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
  // Complete short announcements validated by listening to the bilingual
  // guide corpus. These are ASR repairs, not canonical names or decoder priors.
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

GuideSectionInterpretation interpretGuideSection(
    const GuideTranscriptSegment &segment,
    const std::vector<GuideLearnedMapping> &learnedMappings) {
  if (!validSegment(segment))
    return resolved(SectionType::unknown, {}, "invalid-evidence",
        juce::String::fromUTF8("El anuncio tiene tiempos o confianza inválidos; requiere revisión."));
  auto direct = interpretPhrase(segment.text, learnedMappings);
  if (direct.recognized() || direct.provenance == "ambiguous" ||
      direct.provenance == "compound-announcements") return direct;
  if (isGuideCountCue(segment.text) || isGuideUtilityCue(segment.text) ||
      confirmedSecondaryAlias(segment.text)) return direct;

  double selectedScore = -std::numeric_limits<double>::infinity();
  if (segment.confidence && *segment.confidence > 0.0) selectedScore = std::log(*segment.confidence);
  const auto selectedKey = phraseKey(segment.text);
  for (const auto &alternative : segment.alternatives)
    if (alternative.language == segment.language && phraseKey(alternative.text) == selectedKey &&
        std::isfinite(alternative.avgLogProbability))
      selectedScore = alternative.avgLogProbability;

  std::optional<GuideSectionInterpretation> interpretation;
  if (segment.alternatives.size() <= 2) for (const auto &alternative : segment.alternatives) {
    // Alternatives are evidence, not a licence to replace a clearly stronger
    // unknown transcription with an expected musical word.
    if ((alternative.language != "es" && alternative.language != "en") ||
        !std::isfinite(alternative.avgLogProbability) || alternative.avgLogProbability > 0.0 ||
        alternative.avgLogProbability < -1.2 ||
        alternative.avgLogProbability < selectedScore - 0.2 ||
        !std::isfinite(alternative.noSpeechProbability) || alternative.noSpeechProbability < 0.0 ||
        alternative.noSpeechProbability > 0.4) continue;
    auto candidate = interpretPhrase(alternative.text, learnedMappings);
    if (candidate.provenance == "ambiguous") return candidate;
    if (!candidate.recognized()) continue;
    if (interpretation && (interpretation->type != candidate.type || interpretation->ordinal != candidate.ordinal))
      return resolved(SectionType::unknown, {}, "ambiguous",
          juce::String::fromUTF8("Las alternativas indican secciones distintas; se conserva el anuncio para revisión."));
    candidate.provenance = "alternative-" + candidate.provenance;
    candidate.reason = juce::String::fromUTF8("Interpretación apoyada por una alternativa de voz cercana. ") +
        candidate.reason + juce::String::fromUTF8(" El texto y tiempo ASR originales se conservan.");
    interpretation = std::move(candidate);
  }
  if (interpretation) return *interpretation;
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

GuideAnnouncementInterpretation interpretGuideAnnouncement(
    const GuideTranscriptSegment &segment,
    const std::vector<GuideLearnedMapping> &learnedMappings) {
  GuideAnnouncementInterpretation result;
  if (validSegment(segment)) {
    auto cue = normalizeGuideCountCue(segment.startSeconds, segment.text, segment.confidence);
    result.kind = GuideAnnouncementKind::count;
    if (!cue) {
      cue = normalizeGuideUtilityCue(segment.startSeconds, segment.text, segment.confidence);
      result.kind = GuideAnnouncementKind::instruction;
    }
    if (cue) {
      result.secondaryName = cue->name;
      result.provenance = "catalog";
      result.reason = juce::String::fromUTF8("Indicación reconocida en el catálogo; conserva el instante del anuncio.");
      return result;
    }
    if (const auto *alias = confirmedSecondaryAlias(segment.text)) {
      result.kind = alias->kind;
      result.secondaryName = alias->name;
      result.provenance = "confirmed-corpus-alias";
      result.reason = juce::String::fromUTF8("Error de voz corregido mediante escucha confirmada del catálogo de guías. Se conservan el texto ASR y el instante original.");
      return result;
    }
  }
  result.section = interpretGuideSection(segment, learnedMappings);
  result.kind = result.section.recognized() ? GuideAnnouncementKind::section : GuideAnnouncementKind::unknown;
  result.provenance = result.section.provenance;
  result.reason = result.section.reason;
  return result;
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
        normalizeGuideCue(0.0, segment.text)) continue;
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
