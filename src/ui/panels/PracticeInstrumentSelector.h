#pragma once

#include "PracticeWheelSelector.h"
#include "../../data/Localization.h"
#include <functional>
#include <vector>

// The same wheel as key/tempo, with stable IDs and room for instrument names.
class PracticeInstrumentSelector final : public PracticeWheelSelector {
public:
  struct Item {
    juce::String id;
    juce::String name;
  };
  enum class PointerPhase { down, drag, up };
  // Let the panel retain its vertical listening gestures and two-finger zoom.
  // Returning true consumes the pointer and cancels the wheel's selection.
  std::function<bool(const juce::MouseEvent &, PointerPhase)> onPointerEvent;

  PracticeInstrumentSelector() {
    configureRange(0.0, 0.0, 1.0);
    setTitle(L10n::tr("Instrumento de ensayo", "Practice instrument"));
    setDescription(L10n::tr(
        "Arrastra horizontalmente para elegir instrumento. Arriba para enfocar; abajo para reducir.",
        "Drag horizontally to choose an instrument. Swipe up to focus; down to reduce."));
    setTooltip(getDescription());
  }

  void setInstruments(const std::vector<Item> &next, const juce::String &selectedId) {
    bool changed = next.size() != instruments.size();
    for (size_t i = 0; !changed && i < next.size(); ++i)
      changed = next[i].id != instruments[i].id || next[i].name != instruments[i].name;
    if (changed) {
      instruments = next;
      configureRange(0.0, juce::jmax(0, static_cast<int>(instruments.size()) - 1), 1.0);
    }
    int selected = 0;
    for (size_t i = 0; i < instruments.size(); ++i)
      if (instruments[i].id == selectedId) {
        selected = static_cast<int>(i);
        break;
      }
    setSessionValue(selected);
    if (changed) {
      // A reordered/renamed list has no meaningful animation from old indices.
      cancelDrag();
      repaint();
    }
    setEnabled(!instruments.empty());
  }

  juce::String getSelectedInstrumentId() const {
    const int index = juce::roundToInt(getValue());
    return juce::isPositiveAndBelow(index, static_cast<int>(instruments.size()))
        ? instruments[static_cast<size_t>(index)].id : juce::String{};
  }

  juce::String getTextFromValue(double value) override {
    const int index = juce::roundToInt(value);
    return juce::isPositiveAndBelow(index, static_cast<int>(instruments.size()))
        ? instruments[static_cast<size_t>(index)].name
        : L10n::tr("Sin instrumento", "No instrument");
  }

  float getItemSpacing() const override {
    return juce::jmax(itemSpacing,
        juce::jmin(static_cast<float>(getWidth()),
                    juce::jlimit(104.0f, 180.0f, getWidth() * 0.42f)));
  }

  void resized() override {
    juce::Slider::resized();
    cancelDrag();
  }

  void mouseDown(const juce::MouseEvent &event) override {
    const juce::Component::SafePointer<PracticeInstrumentSelector> safe(this);
    if (!routePointer(event, PointerPhase::down) && safe != nullptr)
      PracticeWheelSelector::mouseDown(event);
  }

  void mouseDrag(const juce::MouseEvent &event) override {
    const juce::Component::SafePointer<PracticeInstrumentSelector> safe(this);
    if (!routePointer(event, PointerPhase::drag) && safe != nullptr)
      PracticeWheelSelector::mouseDrag(event);
  }

  void mouseUp(const juce::MouseEvent &event) override {
    const juce::Component::SafePointer<PracticeInstrumentSelector> safe(this);
    if (!routePointer(event, PointerPhase::up) && safe != nullptr)
      PracticeWheelSelector::mouseUp(event);
  }

  // Instruments have no musical "original" to restore on a double tap.
  void mouseDoubleClick(const juce::MouseEvent &) override { cancelDrag(); }

  void mouseMagnify(const juce::MouseEvent &event, float scaleFactor) override {
    cancelDrag();
    if (auto *parent = getParentComponent())
      parent->mouseMagnify(event.getEventRelativeTo(parent), scaleFactor);
  }

  bool keyPressed(const juce::KeyPress &key) override {
    if (isEnabled() && key.getKeyCode() == juce::KeyPress::endKey) {
      setValue(juce::jmax(0, static_cast<int>(instruments.size()) - 1),
               juce::sendNotificationSync);
      return true;
    }
    return PracticeWheelSelector::keyPressed(key);
  }

private:
  bool routePointer(const juce::MouseEvent &event, PointerPhase phase) {
    const juce::Component::SafePointer<PracticeInstrumentSelector> safe(this);
    const auto route = onPointerEvent;
    const bool consumed = route && route(event, phase);
    if (safe == nullptr)
      return true;
    if (consumed)
      cancelDrag();
    return consumed;
  }

  float getSelectionWidth() const override { return getItemSpacing() - 8.0f; }
  bool highlightOriginalValue() const override { return false; }
  double originalValue() const override { return 0.0; }
  void paintItemText(juce::Graphics &g, const juce::String &text,
                     juce::Rectangle<int> bounds) override {
    // Ellipsis on up to two lines, never squeeze long labels horizontally.
    // Slider accessibility retains the full name through getTextFromValue.
    g.drawFittedText(text, bounds.reduced(6, 2), juce::Justification::centred, 2, 1.0f);
  }

  std::vector<Item> instruments;
};
