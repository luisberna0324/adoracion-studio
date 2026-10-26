#include "NativeSongLyricsEditor.h"
#include "../data/Localization.h"
#include "../ui/UiCommon.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace NativeSongLyricsEditor {
namespace {
juce::Array<juce::var> kindDefinitions(const SongLyricsDocument *document) {
  const juce::StringArray labels{L10n::tr("Intro", "Intro"), L10n::tr("Verso", "Verse"),
      L10n::tr("Pre-coro", "Pre-chorus"), L10n::tr("Coro", "Chorus"),
      L10n::tr("Puente", "Bridge"), L10n::tr("Instrumental", "Instrumental"),
      L10n::tr("Outro", "Outro"), L10n::tr("Final", "Ending")};
  const juce::StringArray coloursByKind{"#B4F89C", "#A9E6F5", "#EEB1EC", "#FFC5AB",
                                       "#B6C8FB", "#CBE8A6", "#F2D39D", "#E8B9CC"};
  const auto kinds = songLyricsSectionKinds();
  const juce::StringArray codes{"IN", "V", "PC", "C", L10n::tr("P", "B"), "I", "O", L10n::tr("F", "E")};
  std::map<juce::String, int> totals;
  if (document != nullptr)
    for (const auto &section : document->sections) ++totals[section.kind];
  juce::Array<juce::var> entries;
  for (int i = 0; i < static_cast<int>(kinds.size()); ++i) {
    auto *entry = new juce::DynamicObject();
    entry->setProperty("id", kinds[static_cast<size_t>(i)]);
    entry->setProperty("title", labels[i]);
    entry->setProperty("color", coloursByKind[i]);
    entry->setProperty("code", codes[i]);
    const juce::String kind(kinds[static_cast<size_t>(i)]);
    entry->setProperty("numbered", kind == "verse" || kind == "chorus" || kind == "prechorus" || totals[kind] > 1);
    entries.add(juce::var(entry));
  }
  return entries;
}
} // namespace

juce::var sectionLabels(const SongLyricsDocument &document) {
  const auto definitions = kindDefinitions(&document);
  auto ordered = document.sections;
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.start < b.start; });
  std::map<juce::String, int> occurrences;
  juce::Array<juce::var> labels;
  for (const auto &section : ordered) {
    for (const auto &definition : definitions) {
      if (definition.getProperty("id", {}).toString() != section.kind) continue;
      const auto occurrence = ++occurrences[section.kind];
      const auto suffix = static_cast<bool>(definition.getProperty("numbered", false))
          ? juce::String(occurrence) : juce::String();
      const auto title = definition.getProperty("title", {}).toString();
      auto *label = new juce::DynamicObject();
      label->setProperty("id", section.id);
      label->setProperty("kind", section.kind);
      label->setProperty("title", title);
      label->setProperty("label", title + (suffix.isNotEmpty() ? " " + suffix : juce::String()));
      label->setProperty("code", definition.getProperty("code", {}).toString() + suffix);
      label->setProperty("occurrence", occurrence);
      label->setProperty("color", definition.getProperty("color", {}));
      labels.add(juce::var(label));
      break;
    }
  }
  return juce::var(std::move(labels));
}

juce::var presentationConfig(const Options &options) {
  auto *config = new juce::DynamicObject();
  config->setProperty("title", options.title);
  const auto artist = options.artist.trim();
  const auto tempo = std::isfinite(options.tempoBpm) && options.tempoBpm > 0.0 ? options.tempoBpm : 0.0;
  const auto tempoText = tempo > 0.0
      ? juce::String(tempo, std::abs(tempo - std::round(tempo)) < 0.0001 ? 0 : 1) + " BPM"
      : juce::String();
  auto subtitle = artist;
  if (tempoText.isNotEmpty()) {
    if (subtitle.isNotEmpty()) subtitle += juce::String::fromUTF8(u8" · ");
    subtitle += tempoText;
  }
  config->setProperty("artist", artist);
  config->setProperty("tempoBpm", tempo);
  config->setProperty("tempoText", tempoText);
  config->setProperty("subtitle", subtitle);
  config->setProperty("heading", L10n::tr("Letra", "Lyrics"));
  config->setProperty("placeholder", L10n::tr(
      "Pega aquí la letra de la canción o empieza a escribir.\n\nSelecciona un fragmento y elige Sección para marcar Intro, Verso, Coro o Puente.",
      "Paste the song's lyrics here or start writing.\n\nSelect a passage and choose Section to mark an Intro, Verse, Chorus or Bridge."));
  config->setProperty("saveTitle", L10n::tr("Listo", "Done"));
  config->setProperty("closeTitle", L10n::tr("Cerrar", "Close"));
  config->setProperty("sectionActionTitle", L10n::tr("Sección", "Section"));
  config->setProperty("clearSectionTitle", L10n::tr("Quitar sección", "Remove section"));
  config->setProperty("errorTitle", L10n::tr("No se pudo guardar", "Couldn't save"));
  config->setProperty("savingTitle", L10n::tr("Guardando…", "Saving…"));
  config->setProperty("discardTitle", L10n::tr("¿Descartar los cambios?", "Discard changes?"));
  config->setProperty("discardMessage", L10n::tr("La letra conserva los últimos cambios que guardaste.", "The lyrics will keep the changes you last saved."));
  config->setProperty("discardConfirmTitle", L10n::tr("Descartar", "Discard"));
  config->setProperty("keepEditingTitle", L10n::tr("Seguir editando", "Keep editing"));
  auto *colours = new juce::DynamicObject();
  colours->setProperty("background", "#" + UITheme::panelDark.toDisplayString(false));
  colours->setProperty("text", "#" + UITheme::textPrimary.toDisplayString(false));
  colours->setProperty("secondary", "#" + UITheme::textSecondary.toDisplayString(false));
  colours->setProperty("accent", "#" + UITheme::accent.toDisplayString(false));
  colours->setProperty("pageBackground", "#" + UITheme::appBg.toDisplayString(false));
  colours->setProperty("pageBackgroundTop", "#" + UITheme::appBgTop.toDisplayString(false));
  colours->setProperty("pageBackgroundBottom", "#" + UITheme::appBgBottom.toDisplayString(false));
  colours->setProperty("cardBackground", "#" + UITheme::cardBg.toDisplayString(false));
  colours->setProperty("cardBorder", "#" + UITheme::cardBorder.toDisplayString(false));
  // Section headers retain a light pastel fill in every app theme.
  colours->setProperty("sectionHeaderText", "#27302B");
  config->setProperty("colors", juce::var(colours));
  config->setProperty("sectionKinds", kindDefinitions(options.document ? &*options.document : nullptr));
  config->setProperty("sectionLabels", options.document ? sectionLabels(*options.document)
                                                       : juce::var(juce::Array<juce::var>{}));
  return juce::var(config);
}

#if !JUCE_IOS && !JUCE_ANDROID
bool isAvailable() { return false; }
std::unique_ptr<Handle> present(juce::Component &, Options, Save, std::function<void()>) {
  return {};
}
#endif
} // namespace NativeSongLyricsEditor
