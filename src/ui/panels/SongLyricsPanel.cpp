#include "SongLyricsPanel.h"
#include "../../data/Localization.h"
#include <algorithm>
#include <limits>
#include <map>

namespace {
float textWidth(const juce::Font &font, const juce::String &text) {
  return juce::GlyphArrangement::getStringWidth(font, text);
}
void styleLyricsButton(juce::TextButton &button, bool primary = false) {
  button.setColour(juce::TextButton::buttonColourId,
                   primary ? UITheme::accent : UITheme::cardBgHover);
  button.setColour(juce::TextButton::textColourOffId,
                   primary ? UITheme::onAccent : UITheme::textPrimary);
}
SongLyricsBlock emptyBlock(const juce::String &label) {
  SongLyricsBlock block;
  block.id = juce::Uuid().toString();
  block.label = label;
  SongLyricsLine line;
  line.id = juce::Uuid().toString();
  block.lines.push_back(std::move(line));
  return block;
}
}

class SongLyricsPanel::Preview final : public juce::Component {
public:
  explicit Preview(SongLyricsPanel &ownerIn) : owner(ownerIn) {}
  struct Item {
    juce::Rectangle<float> bounds;
    juce::String text, chordId;
    int block = 0, line = 0, position = 0;
    bool chord = false, heading = false;
  };
  void rebuild(int width) {
    if (!dirty && layoutWidth == width) return;
    dirty = false; layoutWidth = width;
    items.clear();
    const float margin = 14.0f;
    const float usable = juce::jmax(30.0f, static_cast<float>(width) - margin * 2.0f);
    const auto font = makeAppFont(owner.readOnly ? 20.0f : 18.0f, AppFontWeight::Regular);
    const auto chordFont = makeAppFont(14.0f, AppFontWeight::SemiBold);
    std::map<juce::juce_wchar, float> glyphWidths;
    float y = 12.0f;
    for (int b = 0; b < static_cast<int>(owner.draft.blocks.size()); ++b) {
      if (!owner.readOnly && b != owner.activeBlock) continue;
      const auto &block = owner.draft.blocks[static_cast<size_t>(b)];
      items.push_back({{margin, y, usable, 30}, block.label, {}, b, 0, 0, false, true});
      y += 38;
      for (int l = 0; l < static_cast<int>(block.lines.size()); ++l) {
        const auto &line = block.lines[static_cast<size_t>(l)];
        const auto characters = line.text.toUTF32();
        const int length = line.text.length();
        std::vector<float> advances(static_cast<size_t>(length + 1), 0.0f);
        std::vector<juce::String> glyphs;
        glyphs.reserve(static_cast<size_t>(length));
        for (int i = 0; i < length; ++i) {
          glyphs.push_back(juce::String::charToString(characters[i]));
          auto measured = glyphWidths.find(characters[i]);
          if (measured == glyphWidths.end()) measured = glyphWidths.emplace(characters[i], textWidth(font, glyphs.back())).first;
          advances[static_cast<size_t>(i + 1)] = advances[static_cast<size_t>(i)] + measured->second;
        }
        std::vector<const SongLyricsChord *> sortedChords;
        for (const auto &chord : line.chords) sortedChords.push_back(&chord);
        std::stable_sort(sortedChords.begin(), sortedChords.end(), [](auto *left, auto *right) { return left->position < right->position; });
        size_t nextChord = 0;
        int start = 0;
        do {
          int end = start;
          int lastSpace = -1;
          while (end < length) {
            if (advances[static_cast<size_t>(end + 1)] - advances[static_cast<size_t>(start)] > usable && end > start) break;
            if (juce::CharacterFunctions::isWhitespace(characters[end])) lastSpace = end;
            ++end;
          }
          if (end < length && lastSpace > start) end = lastSpace + 1;
          if (end == start && end < length) ++end;
          std::vector<float> laneEnds;
          std::vector<Item> chords;
          while (nextChord < sortedChords.size()) {
            const auto &chord = *sortedChords[nextChord];
            if (chord.position > end || (chord.position == end && end < length)) break;
            ++nextChord;
            if (chord.position < start) continue;
            const float chordWidth = juce::jmin(usable, juce::jmax(28.0f, textWidth(chordFont, chord.symbol) + 12));
            const float x = juce::jmin(margin + usable - chordWidth,
                margin + advances[static_cast<size_t>(juce::jlimit(start, end, chord.position))] - advances[static_cast<size_t>(start)]);
            int lane = 0;
            while (lane < static_cast<int>(laneEnds.size()) && laneEnds[static_cast<size_t>(lane)] > x - 4) ++lane;
            if (lane == static_cast<int>(laneEnds.size())) laneEnds.push_back(0);
            laneEnds[static_cast<size_t>(lane)] = x + chordWidth;
            chords.push_back({{x, y + static_cast<float>(lane) * 25.0f, chordWidth, 23}, chord.symbol, chord.id,
                              b, l, chord.position, true, false});
          }
          const float textY = y + juce::jmax(1, static_cast<int>(laneEnds.size())) * 25.0f;
          for (int p = start; p <= end; ++p) {
            const float x = margin + advances[static_cast<size_t>(p)] - advances[static_cast<size_t>(start)];
            const float nextX = margin + advances[static_cast<size_t>(juce::jmin(p + 1, end))] - advances[static_cast<size_t>(start)];
            items.push_back({{x, textY, juce::jmax(8.0f, nextX - x), 28},
                             p < end ? glyphs[static_cast<size_t>(p)] : juce::String(), {}, b, l, p, false, false});
          }
          items.insert(items.end(), chords.begin(), chords.end());
          y = textY + 40;
          start = end;
        } while (start < line.text.length());
      }
      y += 18;
    }
    setSize(juce::jmax(1, width), juce::jmax(1, juce::roundToInt(y)));
    repaint();
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(UITheme::cardBg);
    const auto clip = g.getClipBounds().toFloat();
    for (const auto &item : items) {
      if (!clip.intersects(item.bounds)) continue;
      if (item.heading) {
        g.setColour(UITheme::textSecondary);
        g.setFont(makeAppFont(14, AppFontWeight::SemiBold));
      } else if (item.chord) {
        const bool selected = item.chordId == owner.selectedChordId;
        g.setColour((selected ? UITheme::accent : UITheme::cardBgHover));
        g.fillRoundedRectangle(item.bounds, 5);
        g.setColour(selected ? UITheme::onAccent : UITheme::accent);
        g.setFont(makeAppFont(14, AppFontWeight::SemiBold));
      } else {
        if (!owner.readOnly && item.block == owner.activeBlock && item.line == owner.selectedLine &&
            item.position == owner.selectedPosition) {
          g.setColour(UITheme::accent.withAlpha(0.2f));
          g.fillRoundedRectangle(item.bounds, 3);
        }
        g.setColour(UITheme::textPrimary);
        g.setFont(makeAppFont(owner.readOnly ? 20.0f : 18.0f, AppFontWeight::Regular));
      }
      g.drawText(item.text, item.bounds, item.chord ? juce::Justification::centred : juce::Justification::centredLeft, false);
    }
  }
  void mouseDown(const juce::MouseEvent &event) override {
    draggedChord.clear();
    if (owner.readOnly) return;
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
      if (it->heading || !it->bounds.expanded(2).contains(event.position)) continue;
      owner.selectPosition(it->block, it->line, it->position, it->chordId);
      draggedChord = it->chordId;
      setViewportIgnoreDragFlag(draggedChord.isNotEmpty());
      return;
    }
  }
  void mouseDrag(const juce::MouseEvent &event) override {
    if (owner.readOnly || draggedChord.isEmpty() || event.getDistanceFromDragStart() < 4) return;
    const Item *nearest = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (const auto &item : items) {
      if (item.heading || item.chord || item.block != owner.activeBlock || item.line != owner.selectedLine) continue;
      const auto delta = event.position - item.bounds.getCentre();
      const float candidate = std::abs(delta.x) + 3.0f * std::abs(delta.y);
      if (candidate < distance) { distance = candidate; nearest = &item; }
    }
    if (nearest != nullptr) {
      const auto position = nearest->position;
      if (auto *chord = owner.selectedChord()) {
        chord->position = position;
        owner.selectedPosition = position;
        owner.refreshPreview();
      }
    }
  }
  void mouseUp(const juce::MouseEvent &) override { draggedChord.clear(); setViewportIgnoreDragFlag(false); }
  SongLyricsPanel &owner;
  bool dirty = true;
  int layoutWidth = -1;
  std::vector<Item> items;
  juce::String draggedChord;
};

SongLyricsPanel::SongLyricsPanel(juce::String title, std::optional<SongLyricsDocument> document,
                               bool readOnlyIn, Apply apply, std::function<void()> close)
    : draft(document.value_or(SongLyricsDocument{})), readOnly(readOnlyIn),
      onApply(std::move(apply)), onClose(std::move(close)), preview(std::make_unique<Preview>(*this)) {
  if (draft.blocks.empty() && !readOnly) draft.blocks.push_back(emptyBlock(L10n::tr("Verso", "Verse")));
  heading.setText(L10n::tr("Letra y acordes", "Lyrics and chords"), juce::dontSendNotification);
  heading.setFont(makeAppFont(20, AppFontWeight::SemiBold));
  songLabel.setText(title, juce::dontSendNotification);
  songLabel.setFont(makeAppFont(13, AppFontWeight::Regular));
  hint.setText(L10n::tr("Toca una palabra para colocar un acorde. Arrástralo para moverlo.",
                       "Tap a word to add a chord. Drag a chord to move it."), juce::dontSendNotification);
  hint.setFont(makeAppFont(12, AppFontWeight::Regular));
  for (auto *label : {&heading, &songLabel, &hint}) {
    label->setColour(juce::Label::textColourId, label == &heading ? UITheme::textPrimary : UITheme::textSecondary);
    label->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*label);
  }
  addBlockButton.setButtonText("+"); removeBlockButton.setButtonText(L10n::tr("Quitar", "Remove"));
  doneButton.setButtonText(L10n::tr("Listo", "Done"));
  editButton.setButtonText(L10n::tr("Editar texto", "Edit text"));
  previewButton.setButtonText(L10n::tr("Acordes", "Chords"));
  chordApplyButton.setButtonText(L10n::tr("Aplicar", "Apply"));
  chordLeftButton.setButtonText(juce::String::charToString(0x2190));
  chordRightButton.setButtonText(juce::String::charToString(0x2192));
  chordDeleteButton.setButtonText(L10n::tr("Borrar", "Delete"));
  const char *names[] = {"lyricsAddBlock", "lyricsRemoveBlock", "lyricsDone", "lyricsEditText", "lyricsPreview",
                         "lyricsApplyChord", "lyricsChordLeft", "lyricsChordRight", "lyricsDeleteChord"};
  int name = 0;
  for (auto *button : {&addBlockButton, &removeBlockButton, &doneButton, &editButton, &previewButton,
                       &chordApplyButton, &chordLeftButton, &chordRightButton, &chordDeleteButton}) {
    button->setName(names[name++]);
    styleLyricsButton(*button, button == &doneButton || button == &chordApplyButton);
    addAndMakeVisible(*button);
  }
  addAndMakeVisible(closeButton);
  closeButton.setTooltip(L10n::tr("Cancelar edición", "Cancel editing"));
  closeButton.onClick = [this] { finish(false); };
  doneButton.onClick = [this] { finish(true); };
  addBlockButton.onClick = [this] { addBlock(); };
  removeBlockButton.onClick = [this] { removeBlock(); };
  editButton.onClick = [this] { previewMode = false; resized(); };
  previewButton.onClick = [this] {
    if (syncActiveBlock()) { updateBlockChoices(); refreshPreview(); }
    bodyEditor.giveAwayKeyboardFocus(); previewMode = true; resized();
  };
  chordApplyButton.onClick = [this] { applyChord(); };
  chordLeftButton.onClick = [this] { moveChord(-1); };
  chordRightButton.onClick = [this] { moveChord(1); };
  chordDeleteButton.onClick = [this] { deleteChord(); };
  for (auto *editor : {&blockName, &bodyEditor, &chordEditor}) {
    applyTextEditorTheme(*editor);
    editor->setFont(makeAppFont(editor == &bodyEditor ? 17.0f : 15.0f, AppFontWeight::Regular));
    addAndMakeVisible(*editor);
  }
  blockName.setInputRestrictions(128);
  bodyEditor.setInputRestrictions(262144);
  blockName.setName("lyricsBlockName"); bodyEditor.setName("lyricsText"); chordEditor.setName("lyricsChordSymbol");
  blockName.setTextToShowWhenEmpty(L10n::tr("Nombre del bloque", "Block name"), UITheme::textSecondary);
  bodyEditor.setMultiLine(true, true);
  bodyEditor.setReturnKeyStartsNewLine(true);
  bodyEditor.setScrollbarsShown(true);
  bodyEditor.setTextToShowWhenEmpty(L10n::tr("Escribe o pega aquí la letra de este bloque…", "Type or paste this block's lyrics here…"), UITheme::textSecondary);
  chordEditor.setTextToShowWhenEmpty(juce::String::fromUTF8(u8"C, Am, G/B…"), UITheme::textSecondary);
  chordEditor.setInputRestrictions(32);
  const auto syncText = [this] {
    if (syncActiveBlock()) { updateBlockChoices(); refreshPreview(); }
  };
  blockName.onTextChange = syncText;
  bodyEditor.onTextChange = syncText;
  chordEditor.onReturnKey = [this] { applyChord(); };
  chordEditor.onTextChange = [this] { updateChordActions(); };
  applyComboBoxTheme(blockBox);
  blockBox.setName("lyricsBlockSelector"); addAndMakeVisible(blockBox);
  blockBox.onChange = [this] { if (!updating) selectBlock(blockBox.getSelectedId() - 1); };
  previewViewport.setViewedComponent(preview.get(), false);
  previewViewport.setScrollBarsShown(true, false);
  previewViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
  addAndMakeVisible(previewViewport);
  updateBlockChoices();
  if (!draft.blocks.empty()) selectBlock(0);
  updateChordActions();
  setWantsKeyboardFocus(true);
  startTimerHz(10);
}

SongLyricsPanel::~SongLyricsPanel() { stopTimer(); previewViewport.setViewedComponent(nullptr, false); }
void SongLyricsPanel::parentSizeChanged() { if (auto *parent = getParentComponent()) setBounds(parent->getLocalBounds()); }
void SongLyricsPanel::paint(juce::Graphics &g) {
  g.fillAll(juce::Colours::black.withAlpha(0.5f));
  g.setColour(UITheme::panelDark); g.fillRoundedRectangle(cardBounds.toFloat(), getWidth() < 700 ? 0.0f : 18.0f);
}
void SongLyricsPanel::timerCallback() {
  const auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
  const int inset = display != nullptr && useTouchOptimisedControls() ? display->keyboardInsets.getBottom() : 0;
  if (lastKeyboardInset != inset) { lastKeyboardInset = inset; resized(); }
}
void SongLyricsPanel::resized() {
  auto area = getLocalBounds();
  const bool compact = getWidth() < 760;
  if (const auto *display = useTouchOptimisedControls() ? juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds()) : nullptr) {
    const auto safe = display->safeAreaInsets;
    area = juce::BorderSize<int>(safe.getTop(), safe.getLeft(),
        juce::jmax(safe.getBottom(), display->keyboardInsets.getBottom()), safe.getRight()).subtractedFrom(area);
  }
  if (getWidth() >= 700) area = area.reduced(24).withSizeKeepingCentre(juce::jmin(1120, area.getWidth() - 48), juce::jmin(840, area.getHeight() - 48));
  cardBounds = area;
  const bool shortLayout = area.getHeight() < 380;
  area = area.reduced(compact ? 14 : 24, shortLayout ? 8 : 12);
  auto header = area.removeFromTop(44);
  closeButton.setBounds(header.removeFromRight(44));
  if (!readOnly) { header.removeFromRight(8); doneButton.setBounds(header.removeFromRight(72)); }
  heading.setBounds(header);
  songLabel.setVisible(!shortLayout);
  if (!shortLayout) songLabel.setBounds(area.removeFromTop(24));
  area.removeFromTop(shortLayout ? 4 : 8);
  for (auto *component : std::initializer_list<juce::Component *>{&blockBox, &blockName,
       &bodyEditor, &addBlockButton, &removeBlockButton, &doneButton, &editButton, &previewButton,
       &chordEditor, &chordApplyButton, &chordLeftButton, &chordRightButton, &chordDeleteButton, &hint})
    component->setVisible(!readOnly);
  if (!readOnly) {
    blockBox.setVisible(!shortLayout); blockName.setVisible(!shortLayout);
    addBlockButton.setVisible(!shortLayout); removeBlockButton.setVisible(!shortLayout);
    if (!shortLayout) {
    auto blockRow = area.removeFromTop(40);
    removeBlockButton.setBounds(blockRow.removeFromRight(64)); blockRow.removeFromRight(6);
    addBlockButton.setBounds(blockRow.removeFromRight(40)); blockRow.removeFromRight(6);
    blockBox.setBounds(blockRow);
    area.removeFromTop(6); blockName.setBounds(area.removeFromTop(34)); area.removeFromTop(8);
    }
    if (compact) {
      auto tabs = area.removeFromTop(shortLayout ? 32 : 38); const int half = (tabs.getWidth() - 6) / 2;
      editButton.setBounds(tabs.removeFromLeft(half)); tabs.removeFromLeft(6); previewButton.setBounds(tabs);
      area.removeFromTop(shortLayout ? 4 : 8);
    }
    editButton.setVisible(compact); previewButton.setVisible(compact);
    const bool showChords = !compact || previewMode;
    for (auto *component : std::initializer_list<juce::Component *>{&chordEditor, &chordApplyButton, &chordLeftButton,
                            &chordRightButton, &chordDeleteButton, &hint}) component->setVisible(showChords);
    if (showChords) {
      auto actions = area.removeFromBottom(44);
      chordDeleteButton.setBounds(actions.removeFromRight(56)); actions.removeFromRight(4);
      chordRightButton.setBounds(actions.removeFromRight(36)); actions.removeFromRight(4);
      chordLeftButton.setBounds(actions.removeFromRight(36)); actions.removeFromRight(4);
      chordApplyButton.setBounds(actions.removeFromRight(62)); actions.removeFromRight(6);
      chordEditor.setBounds(actions); area.removeFromBottom(6);
      if (!shortLayout) hint.setBounds(area.removeFromTop(34));
      hint.setVisible(!shortLayout);
    }
    if (!compact) {
      bodyEditor.setBounds(area.removeFromLeft((area.getWidth() - 16) / 2)); area.removeFromLeft(16);
    } else {
      bodyEditor.setBounds(area); bodyEditor.setVisible(!previewMode);
    }
    previewViewport.setVisible(showChords);
  }
  previewViewport.setBounds(area);
  preview->rebuild(juce::jmax(1, previewViewport.getWidth() - 12));
}
void SongLyricsPanel::updateBlockChoices() {
  const bool previous = updating; updating = true;
  blockBox.clear(juce::dontSendNotification);
  for (int i = 0; i < static_cast<int>(draft.blocks.size()); ++i) {
    const auto label = draft.blocks[static_cast<size_t>(i)].label.trim();
    blockBox.addItem(label.isEmpty() ? L10n::tr("Bloque ", "Block ") + juce::String(i + 1) : label, i + 1);
  }
  if (!draft.blocks.empty()) blockBox.setSelectedId(activeBlock + 1, juce::dontSendNotification);
  updating = previous;
}
bool SongLyricsPanel::syncActiveBlock() {
  if (updating || readOnly || !activeBlockLoaded || draft.blocks.empty()) return false;
  auto &block = draft.blocks[static_cast<size_t>(activeBlock)];
  bool changed = block.label != blockName.getText();
  block.label = blockName.getText();
  if (lyricsBlockText(block) != bodyEditor.getText()) {
    replaceLyricsBlockText(block, bodyEditor.getText());
    selectedLine = -1; selectedChordId.clear(); updateChordActions();
    changed = true;
  }
  return changed;
}

void SongLyricsPanel::selectBlock(int index) {
  if (index < 0 || index >= static_cast<int>(draft.blocks.size())) return;
  syncActiveBlock();
  activeBlock = index; updating = true;
  const auto &block = draft.blocks[static_cast<size_t>(index)];
  blockName.setText(block.label, false); bodyEditor.setText(lyricsBlockText(block), false);
  blockBox.setSelectedId(index + 1, juce::dontSendNotification); updating = false; activeBlockLoaded = true;
  selectedLine = -1; selectedChordId.clear(); chordEditor.clear(); updateChordActions();
  previewViewport.setViewPosition(0, 0); refreshPreview();
}
void SongLyricsPanel::refreshPreview() {
  preview->dirty = true;
  if (previewViewport.getWidth() > 0) preview->rebuild(juce::jmax(1, previewViewport.getWidth() - 12));
}
void SongLyricsPanel::addBlock() {
  if (readOnly || draft.blocks.size() >= 256) return;
  syncActiveBlock(); activeBlockLoaded = false;
  draft.blocks.push_back(emptyBlock(L10n::tr("Verso ", "Verse ") + juce::String(static_cast<int>(draft.blocks.size()) + 1)));
  activeBlock = static_cast<int>(draft.blocks.size()) - 1; updateBlockChoices(); selectBlock(activeBlock);
}
void SongLyricsPanel::removeBlock() {
  if (readOnly || draft.blocks.empty()) return;
  syncActiveBlock(); activeBlockLoaded = false;
  draft.blocks.erase(draft.blocks.begin() + activeBlock);
  if (draft.blocks.empty()) draft.blocks.push_back(emptyBlock(L10n::tr("Verso", "Verse")));
  activeBlock = juce::jmin(activeBlock, static_cast<int>(draft.blocks.size()) - 1); updateBlockChoices(); selectBlock(activeBlock);
}
void SongLyricsPanel::selectPosition(int block, int line, int position, const juce::String &chordId) {
  if (readOnly) return;
  if (block != activeBlock) selectBlock(block);
  selectedLine = line; selectedPosition = position; selectedChordId = chordId;
  chordEditor.setText(selectedChord() != nullptr ? selectedChord()->symbol : juce::String(), false);
  updateChordActions(); preview->repaint();
}
SongLyricsChord *SongLyricsPanel::selectedChord() {
  if (activeBlock < 0 || activeBlock >= static_cast<int>(draft.blocks.size())) return nullptr;
  auto &lines = draft.blocks[static_cast<size_t>(activeBlock)].lines;
  if (selectedLine < 0 || selectedLine >= static_cast<int>(lines.size())) return nullptr;
  for (auto &chord : lines[static_cast<size_t>(selectedLine)].chords) if (chord.id == selectedChordId) return &chord;
  return nullptr;
}
void SongLyricsPanel::applyChord() {
  if (readOnly || chordEditor.getText().trim().isEmpty() || selectedLine < 0 || draft.blocks.empty()) return;
  auto &lines = draft.blocks[static_cast<size_t>(activeBlock)].lines;
  if (selectedLine >= static_cast<int>(lines.size())) return;
  const auto symbol = chordEditor.getText().trim();
  if (auto *chord = selectedChord()) chord->symbol = symbol;
  else {
    SongLyricsChord newChord; newChord.id = juce::Uuid().toString(); newChord.symbol = symbol;
    newChord.position = juce::jlimit(0, lines[static_cast<size_t>(selectedLine)].text.length(), selectedPosition);
    selectedChordId = newChord.id; lines[static_cast<size_t>(selectedLine)].chords.push_back(std::move(newChord));
  }
  updateChordActions(); refreshPreview();
}
void SongLyricsPanel::moveChord(int delta) {
  if (readOnly) return;
  if (auto *chord = selectedChord()) {
    chord->position = juce::jlimit(0, draft.blocks[static_cast<size_t>(activeBlock)].lines[static_cast<size_t>(selectedLine)].text.length(), chord->position + delta);
    selectedPosition = chord->position; refreshPreview();
  }
}
void SongLyricsPanel::deleteChord() {
  if (readOnly || selectedChord() == nullptr) return;
  auto &chords = draft.blocks[static_cast<size_t>(activeBlock)].lines[static_cast<size_t>(selectedLine)].chords;
  chords.erase(std::remove_if(chords.begin(), chords.end(), [this](const auto &chord) { return chord.id == selectedChordId; }), chords.end());
  selectedChordId.clear(); chordEditor.clear(); updateChordActions(); refreshPreview();
}
void SongLyricsPanel::updateChordActions() {
  const bool selected = selectedChord() != nullptr;
  chordApplyButton.setEnabled(!readOnly && selectedLine >= 0 && chordEditor.getText().trim().isNotEmpty());
  chordLeftButton.setEnabled(!readOnly && selected); chordRightButton.setEnabled(!readOnly && selected);
  chordDeleteButton.setEnabled(!readOnly && selected);
}
void SongLyricsPanel::finish(bool apply) {
  if (finished) return;
  std::optional<SongLyricsDocument> validated;
  if (apply && !readOnly) {
    syncActiveBlock();
    validated = songLyricsFromVar(songLyricsToVar(draft));
    if (!validated) {
      hint.setText(L10n::tr("Demasiado contenido. Reduce el texto, los bloques o los acordes para guardar.",
                           "Too much content. Reduce text, blocks or chords to save."), juce::dontSendNotification);
      hint.setColour(juce::Label::textColourId, juce::Colour(0xffffb4ab));
      previewMode = true; resized(); return;
    }
  }
  finished = true; bodyEditor.giveAwayKeyboardFocus(); chordEditor.giveAwayKeyboardFocus();
  if (apply && !readOnly && onApply) onApply(hasSongLyrics(validated) ? std::move(validated) : std::nullopt);
  else if (onClose) onClose();
}
bool SongLyricsPanel::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::escapeKey) { finish(false); return true; }
  return false;
}
