#include "GuideSectionInterpretation.h"
#include "GuideSpeechSegmentation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>

namespace playworship::analysis {
namespace {
constexpr int storeSchema = 1;
constexpr size_t maximumMappings = 256;
constexpr juce::int64 maximumStoreBytes = 256 * 1024;
constexpr int maximumRawCharacters = 256;
constexpr int maximumKeyCharacters = 128;
constexpr double evidenceTimeTolerance = 1.0e-6;
std::mutex storeMutex;

const char *familyKey(SectionType type) {
  const auto *entry = playworship::guide::findSectionDefinition(type);
  return entry ? entry->key : nullptr;
}

SectionType familyFromKey(const juce::String &key) {
  for (const auto &entry : playworship::guide::sectionDefinitions())
    if (key == entry.key) return entry.family;
  return SectionType::unknown;
}

juce::String phraseKey(juce::String text) {
  if (text.length() > maximumRawCharacters) return {};
  const auto normalized = playworship::guide::normalizedWords(text);
  auto tokens = juce::StringArray::fromTokens(normalized, " ", "");
  tokens.removeEmptyStrings();
  if (tokens.isEmpty() || tokens.size() > 16) return {};
  const auto key = tokens.joinIntoString(" ");
  return key.length() <= maximumKeyCharacters ? key : juce::String{};
}

struct SecondaryAlias {
  const char *phrase, *name;
  GuideAnnouncementKind kind;
};

const SecondaryAlias *confirmedSecondaryAlias(const juce::String &text) {
  // Complete short announcements confirmed in corpus/song reviews. These are
  // ASR repairs, not canonical names or decoder priors.
  static const SecondaryAlias aliases[]{
      {"base", "Bass", GuideAnnouncementKind::instruction},
      {"balsa", "Break", GuideAnnouncementKind::instruction},
      {"entra a bateria", "Drums In", GuideAnnouncementKind::instruction},
      {"thor", "4", GuideAnnouncementKind::count},
      {"swin", "Swell", GuideAnnouncementKind::instruction}};
  const auto key = phraseKey(text);
  for (const auto &alias : aliases)
    if (key == alias.phrase) return &alias;
  return nullptr;
}

bool validMapping(const GuideLearnedMapping &mapping) {
  if (!familyKey(mapping.type) || phraseKey(mapping.rawText).isEmpty() ||
      guideCompoundCount(mapping.rawText) || guideCompoundDirectives(mapping.rawText)) return false;
  // Corrections do not promote a spoken utility/count into a musical boundary
  // or override a name already understood by the standard section catalogue.
  const auto catalog = normalizeGuideCue(0.0, mapping.rawText);
  return catalog ? catalog->normalizedType == mapping.type
                 : !isGuideCountCue(mapping.rawText) && !isGuideUtilityCue(mapping.rawText) &&
                       !confirmedSecondaryAlias(mapping.rawText);
}

bool validSegment(const GuideTranscriptSegment &segment) {
  return std::isfinite(segment.startSeconds) && segment.startSeconds >= 0.0 &&
      std::isfinite(segment.endSeconds) && segment.endSeconds > segment.startSeconds &&
      (!segment.confidence || (std::isfinite(*segment.confidence) &&
          *segment.confidence >= 0.0 && *segment.confidence <= 1.0));
}

GuideSectionInterpretation resolved(SectionType type, std::optional<int> ordinal,
                                    const juce::String &provenance,
                                    const juce::String &reason) {
  return {type, ordinal, provenance, reason};
}

GuideSectionInterpretation interpretPhrase(
    const juce::String &text, const std::vector<GuideLearnedMapping> &mappings) {
  const auto key = phraseKey(text);
  if (key.isEmpty()) return {};
  if (guideCompoundDirectives(text))
    return resolved(SectionType::unknown, {}, "compound-announcements",
        juce::String::fromUTF8("El fragmento contiene varios anuncios. Deben separarse con evidencia de audio; se conserva el texto completo para revisar todas sus indicaciones."));
  if (const auto cue = normalizeGuideCue(0.0, text))
    return resolved(cue->normalizedType, cue->spokenOrdinal, "catalog",
        juce::String::fromUTF8("Nombre reconocido en el catálogo de secciones."));
  if (isGuideCountCue(text) || isGuideUtilityCue(text) || confirmedSecondaryAlias(text)) return {};

  std::optional<SectionType> learned;
  if (mappings.size() <= maximumMappings) {
    for (const auto &mapping : mappings) {
      if (!validMapping(mapping) || phraseKey(mapping.rawText) != key) continue;
      if (learned && *learned != mapping.type)
        return resolved(SectionType::unknown, {}, "ambiguous",
            juce::String::fromUTF8("Hay correcciones locales contradictorias; requiere revisión."));
      learned = mapping.type;
    }
  }
  if (learned)
    return resolved(*learned, {}, "learned-correction",
        juce::String::fromUTF8("Familia de sección recordada de una corrección local confirmada; el número se calcula para esta canción."));

  // This complete phrase was explicitly corrected by the user. It belongs to
  // interpretation only: decoder scoring, raw text and ASR cache stay intact.
  if (key == "for a frame" || key == "foraframe")
    return resolved(SectionType::refrain, {}, "confirmed-alias",
        juce::String::fromUTF8("Equivalencia confirmada: este anuncio se interpreta como Refrain."));
  struct SectionAlias { const char *phrase; SectionType type; int ordinal; };
  static const SectionAlias aliases[]{
      {"course 2", SectionType::chorus, 2},
      {"exotacion", SectionType::exhortation, 0},
      {"enterlude", SectionType::interlude, 0},
      {"wrap", SectionType::rap, 0},
      {"prab", SectionType::rap, 0}};
  for (const auto &alias : aliases)
    if (key == alias.phrase)
      return resolved(alias.type, alias.ordinal ? std::optional<int>{alias.ordinal} : std::nullopt,
          "confirmed-corpus-alias",
          juce::String::fromUTF8("Error de voz corregido mediante escucha confirmada del catálogo de guías. Se conserva el texto ASR original."));
  return {};
}

GuideAnnouncementInterpretation literalAnnouncement(
    const juce::String &text, const std::vector<GuideLearnedMapping> &mappings) {
  GuideAnnouncementInterpretation result;
  auto cue = normalizeGuideCountCue(0.0, text);
  result.kind = GuideAnnouncementKind::count;
  if (!cue) {
    cue = normalizeGuideUtilityCue(0.0, text);
    result.kind = GuideAnnouncementKind::instruction;
  }
  if (cue) {
    result.secondaryName = cue->name;
    result.provenance = "catalog";
    result.reason = juce::String::fromUTF8("Indicación reconocida en el catálogo; conserva el instante del anuncio.");
    return result;
  }
  if (const auto *alias = confirmedSecondaryAlias(text)) {
    result.kind = alias->kind;
    result.secondaryName = alias->name;
    result.provenance = "confirmed-corpus-alias";
    result.reason = juce::String::fromUTF8("Equivalencia confirmada de una indicación; se conserva la evidencia original.");
    return result;
  }
  result.section = interpretPhrase(text, mappings);
  result.kind = result.section.recognized() ? GuideAnnouncementKind::section : GuideAnnouncementKind::unknown;
  result.provenance = result.section.provenance;
  result.reason = result.section.reason;
  return result;
}

bool plausibleAlternative(const GuideTranscriptAlternative &alternative) {
  return (alternative.language == "es" || alternative.language == "en") &&
      std::isfinite(alternative.avgLogProbability) && alternative.avgLogProbability <= 0.0 &&
      alternative.avgLogProbability >= -1.2 &&
      std::isfinite(alternative.noSpeechProbability) && alternative.noSpeechProbability >= 0.0 &&
      alternative.noSpeechProbability <= 0.4;
}

double selectedDecoderScore(const GuideTranscriptSegment &segment) {
  double score = -std::numeric_limits<double>::infinity();
  if (segment.confidence && *segment.confidence > 0.0) score = std::log(*segment.confidence);
  const auto key = phraseKey(segment.text);
  if (segment.alternatives.size() <= 16)
    for (const auto &alternative : segment.alternatives)
      if (alternative.language == segment.language && phraseKey(alternative.text) == key &&
          std::isfinite(alternative.avgLogProbability) && alternative.avgLogProbability <= 0.0)
        score = alternative.avgLogProbability;
  return score;
}

bool sameAnnouncement(const GuideAnnouncementInterpretation &first,
                      const GuideAnnouncementInterpretation &second) {
  if (first.kind != second.kind) return false;
  return first.kind == GuideAnnouncementKind::section
      ? first.section.type == second.section.type && first.section.ordinal == second.section.ordinal
      : first.secondaryName == second.secondaryName;
}

bool hasPlausibleSecondaryAlternative(const GuideTranscriptSegment &segment) {
  if (segment.alternatives.size() > 16) return false;
  for (const auto &alternative : segment.alternatives) {
    if (!plausibleAlternative(alternative)) continue;
    const auto meaning = literalAnnouncement(alternative.text, {});
    if (meaning.kind == GuideAnnouncementKind::count || meaning.kind == GuideAnnouncementKind::instruction)
      return true;
  }
  return false;
}

int editDistance(const juce::String &first, const juce::String &second) {
  std::vector<int> previous(static_cast<size_t>(second.length() + 1));
  for (size_t i = 0; i < previous.size(); ++i) previous[i] = static_cast<int>(i);
  for (int row = 1; row <= first.length(); ++row) {
    std::vector<int> current(previous.size());
    current[0] = row;
    for (int column = 1; column <= second.length(); ++column)
      current[static_cast<size_t>(column)] = std::min({
          current[static_cast<size_t>(column - 1)] + 1,
          previous[static_cast<size_t>(column)] + 1,
          previous[static_cast<size_t>(column - 1)] + (first[row - 1] == second[column - 1] ? 0 : 1)});
    previous = std::move(current);
  }
  return previous.back();
}

GuideSectionInterpretation closeCatalogCandidate(const juce::String &text) {
  const auto key = phraseKey(text);
  if (key.length() < 4 || key.length() > 14 || key.containsChar(' ') ||
      key.containsAnyOf("0123456789")) return {};
  std::map<SectionType, int> distances;
  for (const auto &entry : playworship::guide::sectionDefinitions()) {
    int best = 100;
    for (const auto *alias : entry.aliases) {
      const auto candidate = phraseKey(alias);
      if (candidate.length() < 4 || candidate.length() > 14 || candidate.containsChar(' ')) continue;
      best = std::min(best, editDistance(key, candidate));
    }
    distances[entry.family] = best;
  }
  SectionType nearest = SectionType::unknown;
  int best = 100, runnerUp = 100;
  for (const auto &entry : distances) {
    if (entry.second < best) {
      runnerUp = best; best = entry.second; nearest = entry.first;
    } else runnerUp = std::min(runnerUp, entry.second);
  }
  if (best != 1 || runnerUp - best < 2) return {};
  auto candidate = resolved(nearest, {}, "catalog-near-match",
      juce::String::fromUTF8("Una palabra está a una edición de una única familia del catálogo; es una posibilidad que debe confirmarse visualmente."));
  candidate.tentative = true;
  return candidate;
}

bool readStore(const juce::File &file, std::vector<GuideLearnedMapping> &out,
               juce::String &error) {
  out.clear(); error.clear();
  if (file.getFullPathName().isEmpty()) {
    error = juce::String::fromUTF8("La ruta de correcciones locales no es válida."); return false;
  }
  if (!file.exists()) return true;
  if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maximumStoreBytes) {
    error = juce::String::fromUTF8("El archivo de correcciones locales no es válido o excede su límite."); return false;
  }
  const auto value = juce::JSON::parse(file);
  const auto *object = value.getDynamicObject();
  if (!object || !object->getProperty("schemaVersion").isInt() ||
      int(object->getProperty("schemaVersion")) != storeSchema) {
    error = juce::String::fromUTF8("El formato de correcciones locales no es compatible; se conserva el archivo original."); return false;
  }
  const auto *items = object->getProperty("mappings").getArray();
  if (!items || static_cast<size_t>(items->size()) > maximumMappings) {
    error = juce::String::fromUTF8("La lista de correcciones locales no es válida o excede su límite."); return false;
  }
  std::vector<GuideLearnedMapping> decoded;
  std::set<juce::String> keys;
  for (const auto &item : *items) {
    const auto *entry = item.getDynamicObject();
    if (!entry || !entry->getProperty("rawText").isString() || !entry->getProperty("family").isString()) {
      error = juce::String::fromUTF8("Una corrección local tiene campos inválidos."); return false;
    }
    GuideLearnedMapping mapping{entry->getProperty("rawText").toString(),
        familyFromKey(entry->getProperty("family").toString())};
    if (!validMapping(mapping) || !keys.insert(phraseKey(mapping.rawText)).second) {
      error = juce::String::fromUTF8("Una corrección local es inválida o está duplicada."); return false;
    }
    decoded.push_back(std::move(mapping));
  }
  out = std::move(decoded);
  return true;
}
} // namespace

bool GuideSectionInterpretation::recognized() const noexcept { return familyKey(type) != nullptr; }

std::vector<GuideCatalogChoice> guideCatalogEntries() {
  static const auto entries = [] {
    std::vector<GuideCatalogChoice> result;
    std::set<std::pair<int, juce::String>> identities;
    const auto append = [&](GuideCatalogChoice choice) {
      if (identities.emplace(int(choice.kind), phraseKey(choice.key)).second)
        result.push_back(std::move(choice));
    };
    for (const auto &entry : playworship::guide::sectionDefinitions()) {
      for (int ordinal = 0; ordinal <= entry.numberedVariantLimit; ++ordinal) {
        GuideCatalogChoice choice;
        choice.kind = GuideAnnouncementKind::section;
        choice.type = entry.family;
        choice.key = entry.key;
        choice.label = juce::String::fromUTF8(entry.analysisLabel);
        if (ordinal > 0) {
          choice.ordinal = ordinal;
          choice.key += " " + juce::String(ordinal);
          choice.label += " " + juce::String(ordinal);
        }
        append(std::move(choice));
      }
    }
    for (const auto &entry : playworship::guide::utilityDefinitions()) {
      GuideCatalogChoice choice;
      choice.kind = GuideAnnouncementKind::instruction;
      choice.key = entry.key;
      choice.label = juce::String::fromUTF8(entry.spanishLabel);
      append(std::move(choice));
    }
    for (const auto *alias : playworship::guide::literalUtilityAliases()) {
      const auto cue = normalizeGuideUtilityCue(0.0, juce::String::fromUTF8(alias));
      if (!cue) continue;
      GuideCatalogChoice choice;
      choice.kind = GuideAnnouncementKind::instruction;
      choice.key = choice.label = cue->name;
      append(std::move(choice));
    }
    for (int number = 1; number <= 7; ++number) {
      GuideCatalogChoice choice;
      choice.kind = GuideAnnouncementKind::count;
      choice.key = choice.label = juce::String(number);
      append(std::move(choice));
    }
    return result;
  }();
  return entries;
}

std::vector<GuideCatalogChoice> guideCatalogChoices(
    const GuideTranscriptSegment &segment, const std::vector<GuideLearnedMapping> &learnedMappings) {
  auto choices = guideCatalogEntries();
  if (!validSegment(segment)) return choices;
  const auto matches = [](const GuideCatalogChoice &choice, const GuideAnnouncementInterpretation &meaning) {
    if (choice.kind != meaning.kind) return false;
    if (choice.kind == GuideAnnouncementKind::section)
      return choice.type == meaning.section.type &&
          (choice.ordinal == meaning.section.ordinal || !choice.ordinal);
    return phraseKey(choice.key) == phraseKey(meaning.secondaryName);
  };
  const auto direct = literalAnnouncement(segment.text, learnedMappings);
  const bool blockedLearned = direct.section.provenance == "learned-correction" &&
      hasPlausibleSecondaryAlternative(segment);
  const auto rawKey = phraseKey(segment.text);
  for (auto &choice : choices) {
    juce::StringArray forms{choice.key, choice.label};
    if (choice.kind == GuideAnnouncementKind::section) {
      if (const auto *entry = playworship::guide::findSectionDefinition(choice.type))
        for (const auto *alias : entry->aliases)
          forms.add(juce::String::fromUTF8(alias) +
              (choice.ordinal ? " " + juce::String(*choice.ordinal) : juce::String{}));
    } else if (choice.kind == GuideAnnouncementKind::instruction) {
      if (const auto *entry = playworship::guide::findUtilityDefinition(choice.key))
        for (const auto *alias : entry->aliases) forms.add(juce::String::fromUTF8(alias));
    } else {
      static const char *english[]{"one", "two", "three", "four", "five", "six", "seven"};
      static const char *spanish[]{"uno", "dos", "tres", "cuatro", "cinco", "seis", "siete"};
      const int number = choice.key.getIntValue();
      forms.add(english[number - 1]); forms.add(spanish[number - 1]);
    }
    if (!rawKey.isEmpty()) for (const auto &form : forms) {
      const auto normalized = phraseKey(form);
      if (normalized.isEmpty()) continue;
      const double similarity = 1.0 - double(editDistance(rawKey, normalized)) /
          double(std::max(rawKey.length(), normalized.length()));
      if (similarity >= 0.5 && similarity > choice.score) {
        choice.score = similarity;
        choice.evidence = "catalog-text-similarity";
      }
    }
    if (direct.recognized() && !blockedLearned && matches(choice, direct)) {
      choice.score = 4.0 + (choice.ordinal == direct.section.ordinal ? .001 : 0.0);
      choice.supported = true;
      choice.evidence = direct.provenance;
    }
  }
  const double selectedScore = selectedDecoderScore(segment);
  const auto rankAlternatives = [&](const std::vector<GuideTranscriptAlternative> &alternatives, bool constrained) {
    if (alternatives.size() > 16) return;
    for (const auto &alternative : alternatives) {
      if (!plausibleAlternative(alternative)) continue;
      const auto meaning = literalAnnouncement(alternative.text, {});
      if (!meaning.recognized()) continue;
      const bool supported = !constrained && alternative.avgLogProbability >= selectedScore - 0.2;
      const double score = (constrained ? 1.5 : supported ? 3.0 : 2.0) +
          alternative.avgLogProbability * (constrained ? .05 : .1);
      for (auto &choice : choices) if (matches(choice, meaning) && score > choice.score) {
        choice.score = score + (choice.ordinal == meaning.section.ordinal ? .001 : 0.0);
        choice.evidence = constrained ? "constrained-asr" :
            supported ? "alternative-catalog" : "alternative-catalog-review";
        choice.supported = supported;
      }
    }
  };
  rankAlternatives(segment.alternatives, false);
  rankAlternatives(segment.catalogueAlternatives, true);
  if (const auto compound = guideCompoundCount(segment.text)) {
    for (auto &choice : choices) {
      if (choice.kind != GuideAnnouncementKind::count) continue;
      if (std::find(compound->counts.begin(), compound->counts.end(), choice.key.getIntValue()) == compound->counts.end()) continue;
      if (choice.score < 2.5) {
        choice.score = 2.5; choice.evidence = "compound-count-review"; choice.supported = false;
      }
    }
  }
  std::stable_sort(choices.begin(), choices.end(), [](const auto &first, const auto &second) {
    return first.score > second.score;
  });
  return choices;
}

GuideSectionInterpretation interpretGuideSection(
    const GuideTranscriptSegment &segment,
    const std::vector<GuideLearnedMapping> &learnedMappings) {
  if (!validSegment(segment))
    return resolved(SectionType::unknown, {}, "invalid-evidence",
        juce::String::fromUTF8("El anuncio tiene tiempos o confianza inválidos; requiere revisión."));
  auto direct = interpretPhrase(segment.text, learnedMappings);
  if (direct.provenance == "learned-correction" && hasPlausibleSecondaryAlternative(segment))
    return resolved(SectionType::unknown, {}, "ambiguous-role",
        juce::String::fromUTF8("Una alternativa de voz corresponde a una indicación o cuenta; la corrección local no puede convertirla automáticamente en sección."));
  if (direct.recognized() || direct.provenance == "ambiguous" ||
      direct.provenance == "compound-announcements") return direct;
  if (isGuideCountCue(segment.text) || isGuideUtilityCue(segment.text) ||
      confirmedSecondaryAlias(segment.text)) return direct;

  const double selectedScore = selectedDecoderScore(segment);
  std::optional<GuideAnnouncementInterpretation> interpretation;
  if (segment.alternatives.size() <= 16) for (const auto &alternative : segment.alternatives) {
    if (!plausibleAlternative(alternative) || alternative.avgLogProbability < selectedScore - 0.2) continue;
    auto candidate = literalAnnouncement(alternative.text, learnedMappings);
    if (candidate.section.provenance == "learned-correction" && hasPlausibleSecondaryAlternative(segment)) continue;
    if (candidate.provenance == "ambiguous") return candidate.section;
    if (!candidate.recognized()) continue;
    if (interpretation && !sameAnnouncement(*interpretation, candidate))
      return resolved(SectionType::unknown, {}, "ambiguous",
          juce::String::fromUTF8("Las alternativas indican marcas o funciones distintas; se conserva el anuncio para revisión."));
    candidate.provenance = "alternative-" + candidate.provenance;
    candidate.section.provenance = candidate.provenance;
    candidate.section.reason = juce::String::fromUTF8("Interpretación apoyada por una alternativa de voz cercana. ") +
        candidate.section.reason + juce::String::fromUTF8(" El texto y tiempo ASR originales se conservan.");
    interpretation = std::move(candidate);
  }
  if (interpretation) {
    if (interpretation->kind == GuideAnnouncementKind::section) return interpretation->section;
    return resolved(SectionType::unknown, {}, "alternative-secondary",
        juce::String::fromUTF8("La alternativa reconocida es una indicación o cuenta y no crea un límite musical."));
  }
  if (hasPlausibleSecondaryAlternative(segment))
    return resolved(SectionType::unknown, {}, "secondary-candidate",
        juce::String::fromUTF8("Hay una alternativa de indicación o cuenta que requiere revisión en el catálogo."));
  auto tentative = closeCatalogCandidate(segment.text);
  if (tentative.recognized()) return tentative;
  return resolved(SectionType::unknown, {}, "unrecognized",
      juce::String::fromUTF8("El anuncio no tiene una equivalencia confirmada; se conserva para revisión sin forzar una sección."));
}

bool GuideAnnouncementInterpretation::recognized() const noexcept {
  return kind == GuideAnnouncementKind::section ? section.recognized()
       : (kind == GuideAnnouncementKind::instruction || kind == GuideAnnouncementKind::count) &&
             secondaryName.isNotEmpty();
}

GuideRecognitionState GuideAnnouncementInterpretation::recognitionState() const {
  if (recognized()) {
    for (const auto &entry : guideCatalogEntries()) {
      if (entry.kind != kind) continue;
      if (kind == GuideAnnouncementKind::section) {
        if (entry.type == section.type && entry.ordinal == section.ordinal)
          return section.tentative ? GuideRecognitionState::tentative : GuideRecognitionState::resolved;
      } else if (entry.key == secondaryName) {
        return GuideRecognitionState::resolved;
      }
    }
    return GuideRecognitionState::reviewOnly;
  }
  if (provenance == "ambiguous" || provenance == "ambiguous-role" ||
      provenance == "compound-announcements" || provenance == "invalid-evidence")
    return GuideRecognitionState::reviewOnly;
  return GuideRecognitionState::unresolved;
}

GuideAnnouncementInterpretation interpretGuideAnnouncement(
    const GuideTranscriptSegment &segment,
    const std::vector<GuideLearnedMapping> &learnedMappings) {
  const auto finish = [&](GuideAnnouncementInterpretation result) {
    result.candidates = guideCatalogChoices(segment, learnedMappings);
    return result;
  };
  GuideAnnouncementInterpretation result;
  if (!validSegment(segment)) {
    result.section = interpretGuideSection(segment, learnedMappings);
    result.provenance = result.section.provenance;
    result.reason = result.section.reason;
    return finish(std::move(result));
  }
  auto direct = literalAnnouncement(segment.text, learnedMappings);
  if (direct.provenance == "compound-announcements" || direct.provenance == "ambiguous")
    return finish(std::move(direct));
  const bool blockedLearned = direct.section.provenance == "learned-correction" &&
      hasPlausibleSecondaryAlternative(segment);
  if (direct.recognized() && !blockedLearned) return finish(std::move(direct));

  const double selectedScore = selectedDecoderScore(segment);
  std::optional<GuideAnnouncementInterpretation> alternativeMeaning;
  if (segment.alternatives.size() <= 16) for (const auto &alternative : segment.alternatives) {
    if (!plausibleAlternative(alternative) || alternative.avgLogProbability < selectedScore - 0.2) continue;
    auto meaning = literalAnnouncement(alternative.text, learnedMappings);
    if (meaning.section.provenance == "learned-correction" && hasPlausibleSecondaryAlternative(segment)) continue;
    if (!meaning.recognized()) continue;
    if (alternativeMeaning && !sameAnnouncement(*alternativeMeaning, meaning)) {
      result.provenance = "ambiguous";
      result.reason = juce::String::fromUTF8("Las alternativas indican marcas o funciones distintas; elige una opción del catálogo.");
      result.section.provenance = result.provenance;
      result.section.reason = result.reason;
      return finish(std::move(result));
    }
    meaning.provenance = "alternative-" + meaning.provenance;
    meaning.reason = juce::String::fromUTF8("Marca apoyada por una alternativa de voz cercana; conserva el texto y el tiempo originales.");
    if (meaning.kind == GuideAnnouncementKind::section) {
      meaning.section.provenance = meaning.provenance;
      meaning.section.reason = meaning.reason;
    }
    alternativeMeaning = std::move(meaning);
  }
  if (alternativeMeaning) return finish(std::move(*alternativeMeaning));
  result.section = interpretGuideSection(segment, learnedMappings);
  result.kind = result.section.recognized() ? GuideAnnouncementKind::section : GuideAnnouncementKind::unknown;
  result.provenance = result.section.provenance;
  result.reason = result.section.reason;
  return finish(std::move(result));
}

juce::String guideSectionSuggestionId(const GuideTranscript &transcript,
                                     const GuideTranscriptSegment &segment) {
  const auto identity = transcript.provider + ":" + transcript.providerVersion + ":" + transcript.audioFingerprint + ":" +
      transcript.modelFingerprint + ":" + juce::String(segment.startSeconds, 12) + ":" +
      juce::String(segment.endSeconds, 12) + ":" + segment.text.trim();
  return "guide-" + juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString().substring(0, 20);
}

std::vector<GuideLearnedMapping> guideSectionCorrectionsForConfirmedRenames(
    const GuideTranscript &transcript, const std::vector<SectionSuggestion> &suggestions,
    const std::vector<GuideConfirmedSectionRename> &renames) {
  std::vector<GuideLearnedMapping> result;
  if (suggestions.size() > maximumSectionSuggestions || renames.size() > maximumSectionSuggestions ||
      transcript.segments.size() > 65536) return result;
  std::map<juce::String, std::vector<const GuideTranscriptSegment *>> segmentsById;
  for (const auto &segment : transcript.segments)
    if (validSegment(segment)) segmentsById[guideSectionSuggestionId(transcript, segment)].push_back(&segment);
  std::map<juce::String, std::optional<GuideLearnedMapping>> byPhrase;
  for (const auto &rename : renames) {
    const auto target = normalizeGuideCue(0.0, rename.displayName);
    if (!target) continue;
    const auto suggestion = std::find_if(suggestions.begin(), suggestions.end(), [&](const auto &item) {
      return item.id == rename.suggestionId;
    });
    if (suggestion == suggestions.end() || !suggestion->matchedCueTimeSeconds ||
        suggestion->sources.contains(juce::String::fromUTF8(mergedGuideAnnouncementsSource)) ||
        suggestion->sources.contains("word-interval-counts") ||
        std::count_if(suggestions.begin(), suggestions.end(), [&](const auto &item) { return item.id == rename.suggestionId; }) != 1)
      continue;
    const auto source = segmentsById.find(rename.suggestionId);
    if (source == segmentsById.end() || source->second.size() != 1) continue;
    const auto &segment = *source->second.front();
    if (!std::isfinite(suggestion->rawStartSeconds) || !std::isfinite(*suggestion->matchedCueTimeSeconds) ||
        std::abs(segment.startSeconds - suggestion->rawStartSeconds) > evidenceTimeTolerance ||
        std::abs(segment.startSeconds - *suggestion->matchedCueTimeSeconds) > evidenceTimeTolerance ||
        normalizeGuideCue(0.0, segment.text) || hasPlausibleSecondaryAlternative(segment)) continue;
    GuideLearnedMapping mapping{segment.text, target->normalizedType};
    if (!validMapping(mapping)) continue;
    const auto key = phraseKey(mapping.rawText);
    const auto previous = byPhrase.find(key);
    if (previous == byPhrase.end()) byPhrase.emplace(key, mapping);
    else if (previous->second && previous->second->type != mapping.type) previous->second.reset();
  }
  for (auto &entry : byPhrase) if (entry.second) result.push_back(std::move(*entry.second));
  return result;
}

std::vector<GuideLearnedMapping> guideSectionCorrectionsForConfirmedReview(
    const GuideTranscript &transcript, const std::vector<SectionSuggestion> &suggestions,
    const std::vector<GuideConfirmedSectionRename> &renames) {
  if (suggestions.size() > maximumSectionSuggestions || renames.size() > maximumSectionSuggestions ||
      transcript.segments.size() > 65536) return {};
  auto confirmed = renames;
  for (const auto &suggestion : suggestions) {
    const auto expectedName = "Posible " + sectionTypeName(suggestion.type);
    const bool expectedDisplayName = suggestion.displayName == expectedName ||
        (suggestion.occurrence > 0 && suggestion.displayName == expectedName + " " + juce::String(suggestion.occurrence));
    if (!expectedDisplayName || suggestion.status == SuggestionStatus::rejected ||
        !suggestion.sources.contains(juce::String::fromUTF8(tentativeGuideInterpretationSource)) ||
        std::any_of(renames.begin(), renames.end(), [&](const auto &rename) { return rename.suggestionId == suggestion.id; }))
      continue;
    for (const auto &segment : transcript.segments) {
      if (guideSectionSuggestionId(transcript, segment) != suggestion.id) continue;
      const auto interpretation = interpretGuideSection(segment);
      if (interpretation.recognized() && interpretation.tentative && interpretation.type == suggestion.type)
        confirmed.push_back({suggestion.id, sectionTypeName(interpretation.type)});
      break;
    }
  }
  return guideSectionCorrectionsForConfirmedRenames(transcript, suggestions, confirmed);
}

bool loadGuideSectionLearnedMappings(const juce::File &file,
                                    std::vector<GuideLearnedMapping> &out,
                                    juce::String &error) {
  const std::lock_guard<std::mutex> lock(storeMutex);
  return readStore(file, out, error);
}

bool mergeAndSaveGuideSectionLearnedMappings(
    const juce::File &file, const std::vector<GuideLearnedMapping> &corrections,
    juce::String &error) {
  const std::lock_guard<std::mutex> lock(storeMutex);
  error.clear();
  if (corrections.empty()) return true;
  if (corrections.size() > maximumMappings) {
    error = juce::String::fromUTF8("Hay demasiadas correcciones locales en una sola confirmación."); return false;
  }
  std::vector<GuideLearnedMapping> existing;
  if (!readStore(file, existing, error)) return false;
  std::map<juce::String, GuideLearnedMapping> merged;
  for (const auto &mapping : existing) merged.emplace(phraseKey(mapping.rawText), mapping);
  std::map<juce::String, SectionType> incoming;
  for (const auto &mapping : corrections) {
    if (!validMapping(mapping)) {
      error = juce::String::fromUTF8("Una corrección confirmada no corresponde a una familia de sección válida."); return false;
    }
    const auto key = phraseKey(mapping.rawText);
    const auto previous = incoming.find(key);
    if (previous != incoming.end() && previous->second != mapping.type) {
      error = juce::String::fromUTF8("Una misma frase tiene correcciones contradictorias; no se modificó el archivo local."); return false;
    }
    incoming[key] = mapping.type;
    merged[key] = mapping;
  }
  if (merged.size() > maximumMappings) {
    error = juce::String::fromUTF8("Se alcanzó el límite de correcciones locales; no se modificó el archivo."); return false;
  }
  auto root = juce::var(new juce::DynamicObject());
  root.getDynamicObject()->setProperty("schemaVersion", storeSchema);
  juce::Array<juce::var> entries;
  for (const auto &entry : merged) {
    auto item = juce::var(new juce::DynamicObject());
    item.getDynamicObject()->setProperty("rawText", entry.second.rawText);
    item.getDynamicObject()->setProperty("family", familyKey(entry.second.type));
    entries.add(std::move(item));
  }
  root.getDynamicObject()->setProperty("mappings", std::move(entries));
  const auto json = juce::JSON::toString(root, true);
  if (static_cast<juce::int64>(json.getNumBytesAsUTF8()) > maximumStoreBytes ||
      !file.getParentDirectory().createDirectory()) {
    error = juce::String::fromUTF8("No se pudo preparar el archivo de correcciones locales."); return false;
  }
  juce::TemporaryFile temporary(file);
  if (!temporary.getFile().replaceWithText(json) || !temporary.overwriteTargetFileWithTemporary()) {
    error = juce::String::fromUTF8("No se pudieron guardar las correcciones locales; se conserva el archivo anterior."); return false;
  }
  return true;
}

} // namespace playworship::analysis
