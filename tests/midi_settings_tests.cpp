#include "data/AppSettings.h"
#include "data/ApplicationDataPaths.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(const bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  const auto root = ApplicationDataPaths::isolatedTestRoot();
  const auto settingsDirectory =
      ApplicationDataPaths::applicationDataDirectory().getChildFile("MTControls");
  expect(settingsDirectory.createDirectory().wasOk(),
         "create isolated settings directory");
  const auto settings = settingsDirectory.getChildFile("app_settings.json");
  expect(settings.isAChildOf(root) && !settings.exists(),
         "MIDI settings start in a fresh isolated directory");
  expect(settings.replaceWithText(R"({"marker":"preserve me"})"),
         "write unrelated setting fixture");

  std::vector<MidiBindingSetting> requested{
      {100, 1, 7, MidiBindingSourceType::CC, MidiBindingMode::Absolute},
      {2, 16, 60, MidiBindingSourceType::Note, MidiBindingMode::Toggle},
      {400, 20, 180, MidiBindingSourceType::CC, MidiBindingMode::Momentary},
      {-1, 1, 1, MidiBindingSourceType::CC, MidiBindingMode::Absolute}};
  AppSettings::setMidiBindings(requested);

  const auto saved = AppSettings::getMidiBindings();
  expect(saved.size() == 3, "invalid MIDI targets are not persisted");
  expect(saved[0].targetId == 100 && saved[0].midiChannel == 1 &&
             saved[0].ccNumber == 7 &&
             saved[0].sourceType == MidiBindingSourceType::CC &&
             saved[0].mode == MidiBindingMode::Absolute,
         "CC absolute binding roundtrips");
  expect(saved[1].targetId == 2 && saved[1].midiChannel == 16 &&
             saved[1].ccNumber == 60 &&
             saved[1].sourceType == MidiBindingSourceType::Note &&
             saved[1].mode == MidiBindingMode::Toggle,
         "note toggle binding roundtrips");
  expect(saved[2].targetId == 400 && saved[2].midiChannel == 16 &&
             saved[2].ccNumber == 127 &&
             saved[2].mode == MidiBindingMode::Momentary,
         "out-of-range MIDI source is clamped without losing its mode");

  const auto parsed = juce::JSON::parse(settings);
  expect(parsed.isObject() && parsed["marker"].toString() == "preserve me",
         "writing MIDI bindings preserves unrelated app settings");
  AppSettings::setMidiBindings({});
  expect(AppSettings::getMidiBindings().empty(),
         "clearing MIDI bindings survives reload");
  std::cout << "MIDI settings regression checks passed\n";
}
