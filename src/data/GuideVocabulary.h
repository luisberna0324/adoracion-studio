#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <initializer_list>
#include <vector>

// Shared musical vocabulary. This interprets ordinary labels only; learned
// corrections of recognition mistakes belong to a separate review layer.
namespace playworship::guide {

// Values 0..15 are persisted by section-detection schema 1. Append only.
enum class SectionFamily { unknown, intro, verse, preChorus, chorus, bridge,
                           instrumental, outro, ending, custom,
                           refrain, postChorus, solo, interlude, tag, vamp,
                           turnaround, exhortation, rap };

struct SectionDefinition {
  SectionFamily family;
  const char *key, *spanishLabel, *englishLabel, *analysisLabel, *colour;
  std::initializer_list<const char *> aliases;
  int numberedVariantLimit = 0;
};

inline const std::array<SectionDefinition, 17> &sectionDefinitions() {
  static const std::array<SectionDefinition, 17> definitions{{
      {SectionFamily::intro, "Intro", "Intro", "Intro", "Intro", "ffB66A8A", {"intro", "introduccion", "introduction"}, 0},
      {SectionFamily::verse, "Verse", "Verso", "Verse", "Verso", "ff8A7A68", {"verse", "verso", "estrofa"}, 6},
      {SectionFamily::preChorus, "Pre-Chorus", "Pre-Coro", "Pre-Chorus", "Pre-coro", "ff5E8F76", {"pre chorus", "pre coro", "prechorus", "precoro", "pre"}, 4},
      {SectionFamily::chorus, "Chorus", "Coro", "Chorus", "Coro", "ff6FA36D", {"chorus", "coro", "coros"}, 4},
      {SectionFamily::refrain, "Refrain", "Refrain", "Refrain", "Refrain", "ff6D91A3", {"refrain", "estribillo"}, 0},
      {SectionFamily::postChorus, "Post-Chorus", "Post-Coro", "Post-Chorus", "Post-coro", "ff7D8F6B", {"post chorus", "post coro", "postchorus", "postcoro"}, 0},
      {SectionFamily::bridge, "Bridge", "Puente", "Bridge", "Puente", "ff7D74A8", {"bridge", "puente"}, 4},
      {SectionFamily::instrumental, "Instrumental", "Instrumental", "Instrumental", "Instrumental", "ffA8895E", {"instrumental", "instrument"}, 0},
      {SectionFamily::solo, "Solo", "Solo", "Solo", "Solo", "ffB88455", {"solo"}, 0},
      {SectionFamily::interlude, "Interlude", "Interlude", "Interlude", "Interludio", "ff6E9A91", {"interlude", "interludio"}, 0},
      {SectionFamily::tag, "Tag", "Tag", "Tag", "Tag", "ff9A7FA8", {"tag", "repetir"}, 0},
      {SectionFamily::vamp, "Vamp", "Vamp", "Vamp", "Vamp", "ff7F9B67", {"vamp"}, 0},
      {SectionFamily::outro, "Outro", "Outro", "Outro", "Salida", "ff92786D", {"outro", "salida", "salida final"}, 0},
      {SectionFamily::ending, "Ending", "Final", "Ending", "Final", "ff9B5E75", {"ending", "final"}, 0},
      {SectionFamily::turnaround, "Turnaround", "Turnaround", "Turnaround", "Turnaround", "ff9A866E", {"turnaround", "turn around"}, 0},
      {SectionFamily::exhortation, "Exhortation", "Exhortación", "Exhortation", "Exhortación", "ff8D87AD", {"exhortation", "exhortacion"}, 0},
      {SectionFamily::rap, "Rap", "Rap", "Rap", "Rap", "ffA67A73", {"rap"}, 0},
  }};
  return definitions;
}

struct UtilityDefinition {
  const char *key, *spanishLabel, *englishLabel;
  std::initializer_list<const char *> aliases;
  bool visibleInEditor = false;
};

// Visibility preserves the editor's existing 17 choices and their order.
// A vocabulary entry is not a promise that every language pack has a clip.
inline const std::array<UtilityDefinition, 26> &utilityDefinitions() {
  static const std::array<UtilityDefinition, 26> definitions{{
      {"Build", "Sube intensidad", "Build", {"build", "build up", "sube intensidad", "subir intensidad", "aumenta intensidad", "aumentar intensidad", "sube la intensidad", "subir la intensidad", "aumenta la intensidad", "aumentar la intensidad", "raise intensity", "increase intensity", "raise the intensity", "increase the intensity", "intensity up"}, true},
      {"Slowly Build", "Sube lento", "Slowly Build", {"slowly build", "build slowly", "sube lento", "subir lento", "sube lentamente", "subir lentamente"}, true},
      {"Breakdown", "Baja intensidad", "Breakdown", {"breakdown", "baja intensidad", "bajar intensidad", "reduce intensidad", "reducir intensidad", "baja la intensidad", "bajar la intensidad", "reduce la intensidad", "reducir la intensidad", "lower intensity", "decrease intensity", "lower the intensity", "decrease the intensity", "intensity down", "bring it down"}, true},
      {"Hold", "Sostener", "Hold", {"hold", "sostener", "sosten"}, true},
      {"Softly", "Suave", "Softly", {"softly", "soft", "suave", "suavemente"}, true},
      {"Break", "Pausa", "Break", {"break", "pausa"}, true},
      {"All In", "Toda la banda", "All In", {"all in", "full band", "toda la banda", "banda completa"}, true},
      {"Drums", "Batería", "Drums", {"drums", "bateria"}, true},
      {"Drums In", "Entra batería", "Drums In", {"drums in", "entra bateria", "entra la bateria", "enter drums"}, true},
      {"Bass", "Bajo", "Bass", {"bass", "bajo"}, true},
      {"Guitar", "Guitarra", "Guitar", {"guitar", "guitarra"}, true},
      {"Keys", "Teclado", "Keys", {"keys", "keyboard", "keyboards", "teclado", "teclados"}, true},
      {"Key Change Up", "Sube tono", "Key Change Up", {"key change up", "sube tono", "subir tono", "subetono"}, true},
      {"Key Change Down", "Baja tono", "Key Change Down", {"key change down", "baja tono", "bajar tono"}, true},
      {"Big Ending", "Final grande", "Big Ending", {"big ending", "final grande", "gran final"}, true},
      {"Last Time", "Última vez", "Last Time", {"last time", "ultima vez", "la ultima vez", "one last time"}, true},
      {"A Capella", "A capella", "A Capella", {"a capella", "a cappella", "acapella", "acappella", "a capela", "acapela"}, true},
      {"Ad Lib", "Ad lib", "Ad Lib", {"ad lib", "ad libitum", "adlib"}, false},
      {"All", "All", "All", {"all"}, false},
      {"Big", "Big", "Big", {"big"}, false},
      {"Slowly", "Slowly", "Slowly", {"slowly"}, false},
      {"Hits", "Golpes", "Hits", {"hits", "golpes", "pulsos"}, false},
      {"Key", "Key", "Key", {"key"}, false},
      {"Last", "Last", "Last", {"last"}, false},
      {"Swell", "Swell", "Swell", {"swell"}, false},
      {"Worship Freely", "Adoración libre", "Worship Freely", {"worship freely", "adoracion libre", "adoren libremente"}, false},
  }};
  return definitions;
}

inline const std::vector<UtilityDefinition> &editorUtilityDefinitions() {
  static const auto visible = [] {
    std::vector<UtilityDefinition> result;
    for (const auto &entry : utilityDefinitions())
      if (entry.visibleInEditor) result.push_back(entry);
    return result;
  }();
  return visible;
}

// Known instructions with no exact equivalent clip retain their literal name;
// for example, "solo bajo" must never be rewritten into the different "Bass".
inline const std::array<const char *, 25> &literalUtilityAliases() {
  static const std::array<const char *, 25> aliases{{
      "piano", "bass in", "entra bajo", "entra el bajo", "guitar in", "entra guitarra", "entra la guitarra", "keys in", "entra teclado", "entra el teclado", "piano in", "entra piano", "entra el piano", "solo bateria", "solo bajo", "solo guitarra", "solo teclado", "solo piano", "drums only", "bass only", "guitar only", "keys only", "piano only", "key change", "cambio de tono"
  }};
  return aliases;
}

inline juce::String normalizedWords(juce::String value) {
  // Fold Spanish accents explicitly before lowercasing: native C locales
  // need not lowercase uppercase accented characters. Handle decomposed
  // acute/diaeresis too, without splitting one spoken word into two tokens.
  for (const auto *pair : {"áa", "ée", "íi", "óo", "úu", "üu",
                           "Áa", "Ée", "Íi", "Óo", "Úu", "Üu"}) {
    const auto chars = juce::String::fromUTF8(pair);
    value = value.replaceCharacter(chars[0], chars[1]);
  }
  value = value.toLowerCase();
  juce::String output;
  juce::juce_wchar previous = 0;
  for (auto character : value) {
    if ((character == 0x0301 && juce::String("aeiou").containsChar(previous)) ||
        (character == 0x0308 && previous == 'u')) continue;
    output += juce::CharacterFunctions::isLetterOrDigit(character)
        ? character : static_cast<juce::juce_wchar>(' ');
    previous = character;
  }
  auto tokens = juce::StringArray::fromTokens(output, " ", "");
  tokens.removeEmptyStrings();
  return tokens.joinIntoString(" ");
}

inline const SectionDefinition *findSectionDefinition(SectionFamily family) {
  for (const auto &entry : sectionDefinitions())
    if (entry.family == family) return &entry;
  return nullptr;
}

// Exact label lookup, without numbers or ASR repairs. The spoken-label parser
// handles ordinal grammar separately; native style lookup also strips digits.
inline const SectionDefinition *findSectionDefinition(const juce::String &name) {
  const auto words = normalizedWords(name);
  for (const auto &entry : sectionDefinitions()) {
    if (words == normalizedWords(entry.key)) return &entry;
    for (const auto *alias : entry.aliases)
      if (words == normalizedWords(alias)) return &entry;
  }
  return nullptr;
}

inline const UtilityDefinition *findUtilityDefinition(const juce::String &name) {
  const auto words = normalizedWords(name);
  for (const auto &entry : utilityDefinitions()) {
    if (words == normalizedWords(entry.key)) return &entry;
    for (const auto *alias : entry.aliases)
      if (words == normalizedWords(alias)) return &entry;
  }
  return nullptr;
}

inline juce::String canonicalUtilityName(const juce::String &name) {
  if (const auto *entry = findUtilityDefinition(name)) return entry->key;
  return {};
}

inline bool isKnownUtilityInstruction(const juce::String &name) {
  if (findUtilityDefinition(name)) return true;
  const auto words = normalizedWords(name);
  for (const auto *alias : literalUtilityAliases())
    if (words == alias) return true;
  return false;
}

} // namespace playworship::guide
