#include "SectionDetection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace playworship::analysis {
namespace {
constexpr double epsilon = 1.0e-7;

bool isCancelled(const CancellationCheck &check) { return check && check(); }
bool validTime(double value, double duration) {
  return std::isfinite(value) && value >= 0.0 && value <= duration;
}
bool validGrid(const std::vector<double> &times, double duration) {
  double previous = -1.0;
  for (double value : times) {
    if (!validTime(value, duration) || value <= previous)
      return false;
    previous = value;
  }
  return true;
}

int nearestBar(const std::vector<double> &bars, double seconds) {
  if (bars.empty()) return -1;
  const auto next = std::lower_bound(bars.begin(), bars.end(), seconds);
  if (next == bars.begin()) return 0;
  if (next == bars.end()) return static_cast<int>(bars.size()) - 1;
  return static_cast<int>((seconds - *(next - 1) <= *next - seconds ? next - 1 : next)
                          - bars.begin());
}
double localBarDuration(const std::vector<double> &bars, int index) {
  if (index < 0 || bars.size() < 2) return 0.0;
  return index + 1 < static_cast<int>(bars.size()) ? bars[index + 1] - bars[index]
                                                 : bars[index] - bars[index - 1];
}
std::optional<int> exactBar(const std::vector<double> &bars, double time) {
  const auto index = nearestBar(bars, time);
  if (index >= 0 && std::abs(bars[index] - time) < epsilon) return index;
  return {};
}

juce::String simplified(juce::String value) {
  return playworship::guide::normalizedWords(std::move(value));
}

bool consumePhrase(juce::String &text, const juce::String &phrase) {
  if (text == phrase) { text.clear(); return true; }
  if (!text.startsWith(phrase + " ")) return false;
  text = text.substring(phrase.length() + 1);
  return true;
}

std::optional<int> spokenOrdinal(const juce::String &word) {
  static const std::map<juce::String, int> ordinals{
      {"uno",1},{"un",1},{"una",1},{"primero",1},{"primera",1},{"primer",1},{"one",1},{"first",1},
      {"dos",2},{"segundo",2},{"segunda",2},{"two",2},{"second",2},
      {"tres",3},{"tercero",3},{"tercera",3},{"tercer",3},{"three",3},{"third",3},
      {"cuatro",4},{"cuarto",4},{"cuarta",4},{"four",4},{"fourth",4},
      {"cinco",5},{"quinto",5},{"quinta",5},{"five",5},{"fifth",5},
      {"seis",6},{"sexto",6},{"sexta",6},{"six",6},{"sixth",6},
      {"siete",7},{"septimo",7},{"septima",7},{"seven",7},{"seventh",7},
      {"ocho",8},{"octavo",8},{"octava",8},{"eight",8},{"eighth",8},
      {"nueve",9},{"noveno",9},{"novena",9},{"nine",9},{"ninth",9},
      {"diez",10},{"decimo",10},{"decima",10},{"ten",10},{"tenth",10},
      {"once",11},{"eleven",11},{"eleventh",11},{"doce",12},{"twelve",12},{"twelfth",12},
      {"trece",13},{"thirteen",13},{"thirteenth",13},{"catorce",14},{"fourteen",14},{"fourteenth",14},
      {"quince",15},{"fifteen",15},{"fifteenth",15},{"dieciseis",16},{"sixteen",16},{"sixteenth",16},
      {"diecisiete",17},{"seventeen",17},{"seventeenth",17},{"dieciocho",18},{"eighteen",18},{"eighteenth",18},
      {"diecinueve",19},{"nineteen",19},{"nineteenth",19},{"veinte",20},{"twenty",20},{"twentieth",20}};
  if (const auto found = ordinals.find(word); found != ordinals.end()) return found->second;
  auto digits = word;
  for (const auto *suffix : {"st", "nd", "rd", "th"}) {
    if (digits.endsWith(suffix)) { digits = digits.dropLastCharacters(2); break; }
  }
  if (digits.isEmpty() || digits.length() > 3 || !digits.containsOnly("0123456789")) return {};
  const auto value = digits.getIntValue();
  return value > 0 && value <= 999 ? std::optional<int>{value} : std::optional<int>{};
}

std::optional<int> takeOrdinal(juce::String &text, bool &invalid) {
  const bool numbered = consumePhrase(text, "numero") || consumePhrase(text, "number");
  const auto first = text.upToFirstOccurrenceOf(" ", false, false);
  if (const auto ordinal = spokenOrdinal(first)) {
    consumePhrase(text, first);
    return ordinal;
  }
  invalid = numbered;
  return {};
}

bool countInstruction(const juce::String &text) {
  const auto tokens = juce::StringArray::fromTokens(text, " ", "");
  static const juce::StringArray counts{
      "cero","un","uno","una","dos","tres","cuatro","cinco","seis","siete","ocho","nueve","diez",
      "once","doce","trece","catorce","quince","dieciseis","diecisiete","dieciocho","diecinueve","veinte",
      "zero","one","two","three","four","five","six","seven","eight","nine","ten",
      "eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen","twenty"};
  static const juce::StringArray connectors{"cuenta","conteo","count","in","y","and"};
  bool numberFound = false;
  for (const auto &token : tokens) {
    if (counts.contains(token) || token.containsOnly("0123456789")) numberFound = true;
    else if (!connectors.contains(token)) return false;
  }
  return numberFound;
}

bool countCueText(const juce::String &text) {
  return text == "count in" || text == "countin" || text == "cuenta" || text == "conteo" ||
      countInstruction(text);
}

juce::String canonicalUtilityName(const juce::String &text) {
  return playworship::guide::canonicalUtilityName(text);
}

bool knownUtilityInstruction(const juce::String &text) {
  return playworship::guide::isKnownUtilityInstruction(text) || countCueText(text);
}

struct ParsedGuideLabel {
  SectionType type = SectionType::unknown;
  std::optional<int> ordinal;
};

std::optional<ParsedGuideLabel> parseGuideLabel(juce::String text, bool allowUtilityTail) {
  for (const auto *article : {"el", "la", "the"}) if (consumePhrase(text, article)) break;
  for (const auto *prefix : {"vamos a la seccion", "vamos al", "vamos a", "going to", "go to",
       "next section", "next is", "siguiente seccion", "ahora la seccion", "ahora", "next",
       "section", "seccion", "parte"}) if (consumePhrase(text, prefix)) break;
  for (const auto *article : {"el", "la", "the"}) if (consumePhrase(text, article)) break;
  bool invalid = false;
  auto ordinal = takeOrdinal(text, invalid);
  if (invalid) return {};
  const playworship::guide::SectionDefinition *definition = nullptr;
  juce::String remaining;
  int longestLabel = -1;
  const auto consider = [&](const auto &entry, const juce::String &rawAlias) {
    const auto alias = simplified(rawAlias);
    auto tail = text;
    if (alias.length() > longestLabel && consumePhrase(tail, alias)) {
      definition = &entry;
      remaining = std::move(tail);
      longestLabel = alias.length();
    }
  };
  for (const auto &entry : playworship::guide::sectionDefinitions()) {
    consider(entry, entry.key);
    for (const auto *alias : entry.aliases) consider(entry, alias);
  }
  if (definition == nullptr) return {};
  const auto trailingOrdinal = takeOrdinal(remaining, invalid);
  if (invalid || (ordinal && trailingOrdinal && ordinal != trailingOrdinal)) return {};
  if (!ordinal) ordinal = trailingOrdinal;
  if (remaining.isNotEmpty()) {
    if (!allowUtilityTail) return {};
    // A following sequence of counts is separate speech, not a utility suffix
    // or a section ordinal. Keep it unresolved until source audio can separate
    // the announcements instead of turning "Intro, dos, tres, cuatro" into Intro 2.
    if (countCueText(remaining)) return {};
    for (const auto *connector : {"con", "with", "y", "and"})
      if (consumePhrase(remaining, connector)) break;
    if (countCueText(remaining) || !knownUtilityInstruction(remaining)) return {};
  }
  return ParsedGuideLabel{definition->family, ordinal};
}

bool utilityInstruction(const juce::String &text) {
  if (text.isEmpty()) return false;
  if (knownUtilityInstruction(text)) return true;
  // An instruction remains an instruction when its destination mentions a
  // section: "subir intensidad en el coro" is not a new Chorus boundary.
  for (const auto *separator : {" en ", " durante ", " para ", " al ",
       " in ", " during ", " for ", " at ", " on "}) {
    const auto split = text.indexOf(separator);
    if (split > 0 && knownUtilityInstruction(text.substring(0, split)) &&
        parseGuideLabel(text.substring(split + juce::String(separator).length()), false)) return true;
  }
  return false;
}

juce::var object() { return juce::var(new juce::DynamicObject()); }
void set(juce::var &value, const char *key, const juce::var &property) {
  value.getDynamicObject()->setProperty(key, property);
}
juce::var timesVar(const std::vector<double> &values) {
  juce::Array<juce::var> array;
  for (double value : values) array.add(value);
  return array;
}
juce::var rangesVar(const std::vector<TimeRange> &ranges) {
  juce::Array<juce::var> array;
  for (const auto &range : ranges) {
    auto value = object();
    set(value, "startSeconds", range.startSeconds);
    set(value, "endSeconds", range.endSeconds);
    array.add(value);
  }
  return array;
}
bool number(const juce::var &v) { return v.isDouble() || v.isInt() || v.isInt64(); }
bool readTimes(const juce::var &value, std::vector<double> &out) {
  if (!value.isArray() || value.getArray()->size() > 1000000) return false;
  for (const auto &item : *value.getArray()) {
    if (!number(item) || !std::isfinite(static_cast<double>(item))) return false;
    out.push_back(static_cast<double>(item));
  }
  return true;
}
bool readRanges(const juce::var &value, std::vector<TimeRange> &out) {
  if (!value.isArray() || value.getArray()->size() > 65536) return false;
  for (const auto &item : *value.getArray()) {
    if (!item.isObject() || !number(item["startSeconds"]) || !number(item["endSeconds"]))
      return false;
    out.push_back({static_cast<double>(item["startSeconds"]),
                   static_cast<double>(item["endSeconds"])});
  }
  return true;
}
template <typename Enum> bool readEnum(const juce::var &value, int maximum, Enum &out) {
  if (!value.isInt()) return false;
  const auto index = static_cast<int>(value);
  if (index < 0 || index > maximum) return false;
  out = static_cast<Enum>(index);
  return true;
}
} // namespace

juce::String sectionTypeName(SectionType type) {
  if (const auto *entry = playworship::guide::findSectionDefinition(type))
    return juce::String::fromUTF8(entry->analysisLabel);
  return type == SectionType::custom ? "Personalizada" : juce::String::fromUTF8("Sección sugerida");
}

juce::String confidenceName(ConfidenceLevel level) {
  switch (level) {
  case ConfidenceLevel::high: return "alta";
  case ConfidenceLevel::medium: return "media";
  case ConfidenceLevel::low: return "baja";
  }
  return "baja";
}

std::optional<GuideCue> normalizeGuideCue(double time, const juce::String &raw,
                                         std::optional<double> confidence) {
  if (!std::isfinite(time) || time < 0.0 ||
      (confidence && (!std::isfinite(*confidence) || *confidence < 0 || *confidence > 1)))
    return {};
  const auto text = simplified(raw);
  const auto tokens = juce::StringArray::fromTokens(text, " ", "");
  if (tokens.isEmpty() || tokens.size() > 16 || utilityInstruction(text)) return {};
  const auto label = parseGuideLabel(text, true);
  if (!label) return {};
  GuideCue cue;
  cue.cueTimeSeconds = time;
  cue.rawText = raw;
  cue.asrConfidence = confidence;
  cue.normalizedType = label->type;
  cue.spokenOrdinal = label->ordinal;
  return cue;
}

bool isGuideUtilityCue(const juce::String &raw) {
  const auto text = simplified(raw);
  if (juce::StringArray::fromTokens(text, " ", "").size() > 16) return false;
  return utilityInstruction(text);
}

std::optional<GuideUtilityCue> normalizeGuideUtilityCue(
    double time, const juce::String &raw, std::optional<double> confidence) {
  if (!std::isfinite(time) || time < 0.0 ||
      (confidence && (!std::isfinite(*confidence) || *confidence < 0.0 || *confidence > 1.0)))
    return {};
  const auto text = simplified(raw);
  const auto tokens = juce::StringArray::fromTokens(text, " ", "");
  if (tokens.isEmpty() || tokens.size() > 16 || countCueText(text)) return {};
  auto instruction = text;
  if (!knownUtilityInstruction(instruction)) {
    instruction.clear();
    // Keep an instruction mentioning a musical destination as one point,
    // without also proposing a new musical section for that destination.
    for (const auto *separator : {" en ", " durante ", " para ", " al ",
         " in ", " during ", " for ", " at ", " on "}) {
      const auto split = text.indexOf(separator);
      if (split <= 0) continue;
      const auto before = text.substring(0, split);
      if (!countCueText(before) && knownUtilityInstruction(before) &&
          parseGuideLabel(text.substring(split + juce::String(separator).length()), false)) {
        instruction = before;
        break;
      }
    }
    if (instruction.isEmpty()) return {};
  }
  const auto canonical = canonicalUtilityName(instruction);
  GuideUtilityCue cue;
  cue.cueTimeSeconds = time;
  cue.rawText = raw;
  cue.name = canonical.isNotEmpty() ? canonical : raw;
  cue.asrConfidence = confidence;
  return cue;
}

bool isGuideCountCue(const juce::String &raw) {
  const auto text = simplified(raw);
  if (juce::StringArray::fromTokens(text, " ", "").size() > 16) return false;
  return countCueText(text);
}

std::optional<GuideUtilityCue> normalizeGuideCountCue(
    double time, const juce::String &raw, std::optional<double> confidence) {
  if (!std::isfinite(time) || time < 0.0 ||
      (confidence && (!std::isfinite(*confidence) || *confidence < 0.0 || *confidence > 1.0)) ||
      !isGuideCountCue(raw))
    return {};
  GuideUtilityCue cue;
  cue.cueTimeSeconds = time;
  cue.rawText = cue.name = raw;
  cue.asrConfidence = confidence;
  const auto text = simplified(raw);
  // Match one cardinal word/digit to the native 1..7 clips only. In
  // particular, "one two three" has no per-word timing and is not expanded.
  struct CountName { const char *text; const char *key; };
  static const CountName individualCounts[]{
      {"1", "1"}, {"uno", "1"}, {"un", "1"}, {"una", "1"}, {"one", "1"},
      {"2", "2"}, {"dos", "2"}, {"two", "2"},
      {"3", "3"}, {"tres", "3"}, {"three", "3"},
      {"4", "4"}, {"cuatro", "4"}, {"four", "4"},
      {"5", "5"}, {"cinco", "5"}, {"five", "5"},
      {"6", "6"}, {"seis", "6"}, {"six", "6"},
      {"7", "7"}, {"siete", "7"}, {"seven", "7"},
  };
  for (const auto &count : individualCounts) {
    if (text != count.text) continue;
    cue.name = count.key;
    break;
  }
  return cue;
}

bool validateMusicStructure(const MusicStructureAnalysis &analysis, double duration,
                            juce::String &error) {
  error.clear();
  if (!std::isfinite(duration) || duration <= 0.0) error = juce::String::fromUTF8("Duración de audio inválida.");
  for (const auto *ranges : {&analysis.sections, &analysis.segments, &analysis.phrases}) {
    double previousEnd = -1.0;
    for (const auto &range : *ranges) {
      if (!validTime(range.startSeconds, duration) || !validTime(range.endSeconds, duration) ||
          range.endSeconds <= range.startSeconds || range.startSeconds < previousEnd - epsilon) {
        error = juce::String::fromUTF8("El proveedor devolvió rangos de estructura inválidos o desordenados.");
        break;
      }
      previousEnd = range.endSeconds;
    }
  }
  if (!validGrid(analysis.tempo.bars, duration) || !validGrid(analysis.tempo.beats, duration))
    error = juce::String::fromUTF8("El proveedor devolvió pulsos o compases inválidos o desordenados.");
  if (analysis.tempo.beatsPerMinute && (!std::isfinite(*analysis.tempo.beatsPerMinute) ||
                                      *analysis.tempo.beatsPerMinute <= 0.0))
    error = juce::String::fromUTF8("El proveedor devolvió BPM inválido.");
  return error.isEmpty();
}

bool fuseSectionSuggestions(SectionDetectionResult &result, const FusionOptions &options,
                            juce::String &error, const CancellationCheck &cancelled) {
  error.clear();
  if (isCancelled(cancelled)) { error = juce::String::fromUTF8("Análisis cancelado."); return false; }
  if (std::any_of(result.suggestions.begin(), result.suggestions.end(), [](const auto &suggestion) {
        return suggestion.userEdited || suggestion.status != SuggestionStatus::suggested;
      })) {
    error = juce::String::fromUTF8("El borrador contiene correcciones o decisiones de revisión. Descártelo explícitamente antes de analizar de nuevo.");
    return false;
  }
  if (!validateMusicStructure(result.structure, result.durationSeconds, error)) return false;
  if (!validGrid(options.confirmedBars, result.durationSeconds) ||
      !std::isfinite(options.maximumSnapSeconds) || options.maximumSnapSeconds < 0 ||
      !std::isfinite(options.maximumSnapFractionOfBar) || options.maximumSnapFractionOfBar < 0) {
    error = juce::String::fromUTF8("Cuadrícula o parámetros de cuantización inválidos."); return false;
  }
  if (result.structure.sections.empty()) {
    error = juce::String::fromUTF8("No se detectaron límites de sección. No se han modificado las secciones.");
    return false;
  }
  const bool confirmed = !options.confirmedBars.empty();
  const auto &bars = confirmed ? options.confirmedBars : result.structure.tempo.bars;
  const auto barSource = confirmed ? options.confirmedGridSource : juce::String("compases del proveedor");
  struct Boundary { double raw, snapped; bool structure; };
  std::vector<Boundary> boundaries{{0.0, 0.0, false},
                                 {result.durationSeconds, result.durationSeconds, false}};
  for (const auto &range : result.structure.sections) {
    if (isCancelled(cancelled)) { error = juce::String::fromUTF8("Análisis cancelado."); return false; }
    for (double time : {range.startSeconds, range.endSeconds}) {
      double snapped = time;
      const auto bar = nearestBar(bars, time);
      if (time > 0.0 && time < result.durationSeconds && bar >= 0) {
        const auto duration = localBarDuration(bars, bar);
        const auto limit = duration > 0.0
            ? juce::jmin(options.maximumSnapSeconds, duration * options.maximumSnapFractionOfBar)
            : 0.0;
        if (std::abs(bars[bar] - time) <= limit) snapped = bars[bar];
      }
      boundaries.push_back({time, snapped, true});
    }
  }
  std::stable_sort(boundaries.begin(), boundaries.end(), [](const auto &a, const auto &b) {
    return a.snapped < b.snapped;
  });
  std::vector<Boundary> unique;
  for (const auto &boundary : boundaries) {
    if (!unique.empty() && std::abs(unique.back().snapped - boundary.snapped) < epsilon) {
      if (boundary.structure && !unique.back().structure) unique.back() = boundary;
      continue;
    }
    unique.push_back(boundary);
  }
  std::vector<SectionSuggestion> suggestions;
  if (unique.size() > maximumSectionSuggestions + 1) {
    error = juce::String::fromUTF8("El análisis excede el límite de 256 secciones del editor. No se han modificado las secciones.");
    return false;
  }
  for (size_t i = 0; i + 1 < unique.size(); ++i) {
    if (unique[i + 1].snapped - unique[i].snapped <= epsilon) continue;
    SectionSuggestion suggestion;
    suggestion.rawStartSeconds = unique[i].raw;
    suggestion.startSeconds = unique[i].snapped;
    suggestion.endSeconds = unique[i + 1].snapped;
    suggestion.quantizationDistanceSeconds = suggestion.startSeconds - suggestion.rawStartSeconds;
    suggestion.startBarIndex = exactBar(bars, suggestion.startSeconds);
    suggestion.endBarIndex = exactBar(bars, suggestion.endSeconds);
    suggestion.sources.add(unique[i].structure ? "estructura" : juce::String::fromUTF8("duración del audio"));
    suggestion.reason = unique[i].structure ? juce::String::fromUTF8("Límite estructural; el proveedor no entrega el nombre musical.")
                                           : "Intervalo sin nombre musical confirmado.";
    if (suggestion.startBarIndex) {
      suggestion.sources.add(barSource);
      if (unique[i].structure) suggestion.confidenceLevel = ConfidenceLevel::medium;
    }
    const auto identity = result.songId + ":" + result.analyzedAudioFingerprint + ":" +
                          juce::String(suggestion.rawStartSeconds, 12);
    suggestion.id = "detected-" + juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8())
                                      .toHexString().substring(0, 20);
    suggestions.push_back(std::move(suggestion));
  }
  std::vector<std::vector<const GuideCue *>> matches(suggestions.size());
  for (const auto &cue : result.guideCues) {
    if (isCancelled(cancelled)) { error = juce::String::fromUTF8("Análisis cancelado."); return false; }
    if (!validTime(cue.cueTimeSeconds, result.durationSeconds) ||
        cue.normalizedType == SectionType::unknown || cue.normalizedType == SectionType::custom)
      continue;
    double best = std::numeric_limits<double>::infinity(), second = best;
    int bestIndex = -1;
    for (size_t i = 0; i < suggestions.size(); ++i) {
      const auto start = suggestions[i].startSeconds;
      const auto bar = nearestBar(bars, start);
      const auto localDuration = localBarDuration(bars, bar);
      const auto delta = start - cue.cueTimeSeconds;
      // A cue can anticipate 0, 1, or 2 *local* bars. Without a grid the
      // association window is deliberately small; no global fixed shift exists.
      if (delta < -(localDuration > 0 ? localDuration * 0.25 : 0.25) ||
          delta > (localDuration > 0 ? localDuration * 2.0 : 1.5)) continue;
      const auto score = std::abs(delta) + (delta < 0 ? 0.25 : 0.0);
      if (score < best) { second = best; best = score; bestIndex = static_cast<int>(i); }
      else if (score < second) second = score;
    }
    if (bestIndex >= 0 && second - best > 0.25) matches[static_cast<size_t>(bestIndex)].push_back(&cue);
  }
  for (size_t i = 0; i < suggestions.size(); ++i) {
    auto &suggestion = suggestions[i];
    if (matches[i].size() == 1) {
      const auto &cue = *matches[i].front();
      suggestion.type = cue.normalizedType;
      suggestion.matchedCueTimeSeconds = cue.cueTimeSeconds;
      suggestion.guideAnticipationSeconds = suggestion.startSeconds - cue.cueTimeSeconds;
      suggestion.sources.add(juce::String::fromUTF8("guía"));
      suggestion.confidenceLevel = ConfidenceLevel::high;
      suggestion.reason = juce::String::fromUTF8("Nombre explícito en guía: «") + cue.rawText + juce::String::fromUTF8("». Alineación heurística revisable.");
    } else if (matches[i].size() > 1) {
      suggestion.reason += juce::String::fromUTF8(" Hay anuncios de guía en conflicto; revise el nombre.");
      suggestion.sources.add(juce::String::fromUTF8("guía ambigua"));
      suggestion.confidenceLevel = ConfidenceLevel::low;
    }
  }
  std::map<SectionType, int> totals, occurrences;
  for (const auto &suggestion : suggestions) ++totals[suggestion.type];
  for (auto &suggestion : suggestions) {
    suggestion.occurrence = ++occurrences[suggestion.type];
    suggestion.displayName = sectionTypeName(suggestion.type);
    if (totals[suggestion.type] > 1) suggestion.displayName += " " + juce::String(suggestion.occurrence);
  }
  if (isCancelled(cancelled)) { error = juce::String::fromUTF8("Análisis cancelado."); return false; }
  result.suggestions = std::move(suggestions);
  return true;
}

SectionAcceptanceResult appendConfirmedSuggestions(const SongArrangementSnapshot &existing,
    const std::vector<SectionSuggestion> &selected, double duration) {
  SectionAcceptanceResult result;
  result.arrangement = existing;
  if (!std::isfinite(duration) || duration <= 0.0) {
    result.error = juce::String::fromUTF8("Duración inválida."); return result;
  }
  // Custom arrangements map source audio nonlinearly. A source-time proposal
  // cannot safely be inserted into them without an explicit mapping editor.
  for (const auto &part : existing.parts) {
    const bool explicitSourceRange = part.sourceEndSec > part.sourceStartSec;
    if (part.sourcePartId.isNotEmpty() ||
        (explicitSourceRange && (std::abs(part.startSec - part.sourceStartSec) > epsilon ||
                                 std::abs(part.endSec - part.sourceEndSec) > epsilon))) {
      result.error = "El arreglo contiene copias o audio desplazado; confirme las secciones en el arreglo original.";
      return result;
    }
  }
  auto candidates = selected;
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
    return a.startSeconds < b.startSeconds;
  });
  std::vector<SongArrangementPartSnapshot> added;
  for (const auto &suggestion : candidates) {
    if (suggestion.status == SuggestionStatus::rejected) continue;
    bool conflict = suggestion.id.isEmpty() || suggestion.displayName.trim().isEmpty() ||
                    !validTime(suggestion.startSeconds, duration) ||
                    !validTime(suggestion.endSeconds, duration) ||
                    suggestion.endSeconds - suggestion.startSeconds < minimumAcceptedSectionDurationSeconds;
    for (const auto *parts : std::array<const std::vector<SongArrangementPartSnapshot> *,2>{&existing.parts, &added})
      for (const auto &part : *parts)
        if (part.id == suggestion.id ||
            (suggestion.startSeconds < part.endSec - epsilon &&
             suggestion.endSeconds > part.startSec + epsilon)) conflict = true;
    if (conflict) { result.conflictingSuggestionIds.add(suggestion.id); continue; }
    SongArrangementPartSnapshot part;
    part.id = suggestion.id;
    part.name = suggestion.displayName;
    part.color = fallbackArrangementPartColorForName(part.name);
    part.startSec = part.sourceStartSec = suggestion.startSeconds;
    part.endSec = part.sourceEndSec = suggestion.endSeconds;
    added.push_back(std::move(part));
  }
  if (!result.conflictingSuggestionIds.isEmpty()) {
    result.error = juce::String::fromUTF8("Las propuestas se superponen con secciones existentes o tienen límites inválidos (duración mínima: 0,25 s). No se aplicó ninguna.");
    return result;
  }
  if (existing.parts.size() + added.size() > maximumSectionSuggestions) {
    result.error = juce::String::fromUTF8("La confirmación excedería el límite de 256 secciones del editor. No se aplicó ninguna.");
    return result;
  }
  for (const auto &part : added) {
    result.arrangement.parts.push_back(part);
    result.addedSuggestionIds.add(part.id);
  }
  std::stable_sort(result.arrangement.parts.begin(), result.arrangement.parts.end(),
                   [](const auto &a, const auto &b) { return a.startSec < b.startSec; });
  return result;
}

std::optional<std::int64_t> secondsToSourceSamples(double seconds, double sampleRate) {
  if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(sampleRate) || sampleRate <= 0)
    return {};
  const auto samples = static_cast<long double>(seconds) * sampleRate;
  if (samples > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) - 0.5L)
    return {};
  return static_cast<std::int64_t>(std::round(samples));
}
std::optional<double> sourceSamplesToSeconds(std::int64_t sample, double sampleRate) {
  if (sample < 0 || !std::isfinite(sampleRate) || sampleRate <= 0) return {};
  return static_cast<double>(sample) / sampleRate;
}
std::optional<double> sourceSampleToTimelineSeconds(std::int64_t sample, double rate, double offset) {
  const auto time = sourceSamplesToSeconds(sample, rate);
  if (!time || !std::isfinite(offset) || !std::isfinite(*time + offset)) return {};
  return *time + offset;
}

juce::var sectionDetectionResultToVar(const SectionDetectionResult &result) {
  auto root = object();
  set(root,"schemaVersion",result.schemaVersion); set(root,"songId",result.songId);
  set(root,"durationSeconds",result.durationSeconds); set(root,"analysisProvider",result.analysisProvider);
  set(root,"analysisVersion",result.analysisVersion);
  set(root,"analyzedAudioFingerprint",result.analyzedAudioFingerprint);
  auto structure = object();
  set(structure,"provider",result.structure.provider); set(structure,"version",result.structure.version);
  set(structure,"sections",rangesVar(result.structure.sections));
  set(structure,"segments",rangesVar(result.structure.segments));
  set(structure,"phrases",rangesVar(result.structure.phrases));
  auto tempo = object();
  set(tempo,"source",result.structure.tempo.source);
  set(tempo,"beats",timesVar(result.structure.tempo.beats));
  set(tempo,"bars",timesVar(result.structure.tempo.bars));
  if (result.structure.tempo.beatsPerMinute) set(tempo,"beatsPerMinute",*result.structure.tempo.beatsPerMinute);
  set(structure,"tempo",tempo); set(root,"structure",structure);
  juce::Array<juce::var> cues;
  for (const auto &cue : result.guideCues) {
    auto value = object();
    set(value,"cueTimeSeconds",cue.cueTimeSeconds); set(value,"rawText",cue.rawText);
    set(value,"normalizedType",static_cast<int>(cue.normalizedType));
    if (cue.spokenOrdinal) set(value,"spokenOrdinal",*cue.spokenOrdinal);
    if (cue.asrConfidence) set(value,"asrConfidence",*cue.asrConfidence);
    cues.add(value);
  }
  set(root,"guideCues",cues);
  juce::Array<juce::var> suggestions;
  for (const auto &suggestion : result.suggestions) {
    auto value = object();
    set(value,"id",suggestion.id); set(value,"type",static_cast<int>(suggestion.type));
    set(value,"displayName",suggestion.displayName); set(value,"occurrence",suggestion.occurrence);
    set(value,"rawStartSeconds",suggestion.rawStartSeconds); set(value,"startSeconds",suggestion.startSeconds);
    set(value,"endSeconds",suggestion.endSeconds);
    if (suggestion.startBarIndex) set(value,"startBarIndex",*suggestion.startBarIndex);
    if (suggestion.endBarIndex) set(value,"endBarIndex",*suggestion.endBarIndex);
    set(value,"quantizationDistanceSeconds",suggestion.quantizationDistanceSeconds);
    if (suggestion.matchedCueTimeSeconds) set(value,"matchedCueTimeSeconds",*suggestion.matchedCueTimeSeconds);
    if (suggestion.guideAnticipationSeconds) set(value,"guideAnticipationSeconds",*suggestion.guideAnticipationSeconds);
    set(value,"sources",juce::var(suggestion.sources));
    set(value,"confidenceLevel",static_cast<int>(suggestion.confidenceLevel));
    set(value,"reason",suggestion.reason); set(value,"status",static_cast<int>(suggestion.status));
    set(value,"userEdited",suggestion.userEdited); suggestions.add(value);
  }
  set(root,"suggestions",suggestions);
  return root;
}

bool sectionDetectionResultFromVar(const juce::var &value, SectionDetectionResult &out,
                                   juce::String &error) {
  error = juce::String::fromUTF8("Resultado de análisis inválido o de otra versión.");
  if (!value.isObject() || !value["schemaVersion"].isInt() ||
      static_cast<int>(value["schemaVersion"]) != sectionDetectionSchemaVersion ||
      !value["songId"].isString() || !number(value["durationSeconds"]) ||
      !value["analysisProvider"].isString() || !value["analysisVersion"].isString() ||
      !value["analyzedAudioFingerprint"].isString()) return false;
  SectionDetectionResult result;
  result.songId=value["songId"].toString(); result.durationSeconds=static_cast<double>(value["durationSeconds"]);
  result.analysisProvider=value["analysisProvider"].toString();
  result.analysisVersion=value["analysisVersion"].toString();
  result.analyzedAudioFingerprint=value["analyzedAudioFingerprint"].toString();
  const auto structure=value["structure"], tempo=structure["tempo"];
  if (!structure.isObject() || !tempo.isObject() ||
      !readRanges(structure["sections"],result.structure.sections) ||
      !readRanges(structure["segments"],result.structure.segments) ||
      !readRanges(structure["phrases"],result.structure.phrases) ||
      !readTimes(tempo["beats"],result.structure.tempo.beats) ||
      !readTimes(tempo["bars"],result.structure.tempo.bars)) return false;
  result.structure.provider=structure["provider"].toString();
  result.structure.version=structure["version"].toString();
  result.structure.tempo.source=tempo["source"].toString();
  if (tempo.hasProperty("beatsPerMinute")) {
    if (!number(tempo["beatsPerMinute"])) return false;
    result.structure.tempo.beatsPerMinute=static_cast<double>(tempo["beatsPerMinute"]);
  }
  if (!validateMusicStructure(result.structure,result.durationSeconds,error)) return false;
  error=juce::String::fromUTF8("Resultado de análisis inválido o de otra versión.");
  if (!value["guideCues"].isArray() || !value["suggestions"].isArray() ||
      value["guideCues"].getArray()->size() > 65536 ||
      value["suggestions"].getArray()->size() > static_cast<int>(maximumSectionSuggestions)) return false;
  for (const auto &item : *value["guideCues"].getArray()) {
    GuideCue cue;
    if (!item.isObject() || !number(item["cueTimeSeconds"]) || !item["rawText"].isString() ||
        !readEnum(item["normalizedType"],static_cast<int>(SectionType::rap),cue.normalizedType)) return false;
    cue.cueTimeSeconds=static_cast<double>(item["cueTimeSeconds"]); cue.rawText=item["rawText"].toString();
    if (!validTime(cue.cueTimeSeconds,result.durationSeconds)) return false;
    if (item.hasProperty("spokenOrdinal")) {
      if (!item["spokenOrdinal"].isInt() || static_cast<int>(item["spokenOrdinal"]) <= 0 ||
          static_cast<int>(item["spokenOrdinal"]) > 999) return false;
      cue.spokenOrdinal=static_cast<int>(item["spokenOrdinal"]);
    }
    if (item.hasProperty("asrConfidence")) {
      const auto confidence=static_cast<double>(item["asrConfidence"]);
      if (!number(item["asrConfidence"]) || !std::isfinite(confidence) || confidence < 0 || confidence > 1) return false;
      cue.asrConfidence=confidence;
    }
    result.guideCues.push_back(std::move(cue));
  }
  juce::StringArray ids;
  double previousEnd = 0.0;
  for (const auto &item : *value["suggestions"].getArray()) {
    SectionSuggestion suggestion;
    if (!item.isObject() || !item["id"].isString() || !item["displayName"].isString() ||
        !readEnum(item["type"],static_cast<int>(SectionType::rap),suggestion.type) ||
        !readEnum(item["confidenceLevel"],static_cast<int>(ConfidenceLevel::high),suggestion.confidenceLevel) ||
        !readEnum(item["status"],static_cast<int>(SuggestionStatus::rejected),suggestion.status) ||
        !number(item["rawStartSeconds"]) || !number(item["startSeconds"]) || !number(item["endSeconds"]) ||
        !number(item["quantizationDistanceSeconds"]) || !item["occurrence"].isInt() ||
        !item["userEdited"].isBool() || !item["sources"].isArray()) return false;
    suggestion.id=item["id"].toString(); suggestion.displayName=item["displayName"].toString();
    suggestion.rawStartSeconds=static_cast<double>(item["rawStartSeconds"]);
    suggestion.startSeconds=static_cast<double>(item["startSeconds"]);
    suggestion.endSeconds=static_cast<double>(item["endSeconds"]);
    suggestion.quantizationDistanceSeconds=static_cast<double>(item["quantizationDistanceSeconds"]);
    suggestion.occurrence=static_cast<int>(item["occurrence"]);
    suggestion.reason=item["reason"].toString(); suggestion.userEdited=static_cast<bool>(item["userEdited"]);
    if (suggestion.id.isEmpty() || ids.contains(suggestion.id) || suggestion.displayName.isEmpty() ||
        !validTime(suggestion.rawStartSeconds,result.durationSeconds) ||
        !validTime(suggestion.startSeconds,result.durationSeconds) ||
        !validTime(suggestion.endSeconds,result.durationSeconds) ||
        suggestion.endSeconds <= suggestion.startSeconds || suggestion.startSeconds < previousEnd - epsilon ||
        !std::isfinite(suggestion.quantizationDistanceSeconds) || suggestion.occurrence < 1 ||
        std::abs(suggestion.startSeconds - suggestion.rawStartSeconds - suggestion.quantizationDistanceSeconds) > epsilon)
      return false;
    for (const auto &source : *item["sources"].getArray()) {
      if (!source.isString()) return false;
      suggestion.sources.add(source.toString());
    }
    for (const auto *key : {"startBarIndex","endBarIndex"}) {
      if (!item.hasProperty(key)) continue;
      if (!item[key].isInt() || static_cast<int>(item[key]) < 0) return false;
      // Confirmed grids live in fusion options and are absent from this payload;
      // bound indices here and re-fuse raw evidence against the current grid.
      if (static_cast<int>(item[key]) > 1000000) return false;
      (juce::String(key)=="startBarIndex" ? suggestion.startBarIndex : suggestion.endBarIndex)=static_cast<int>(item[key]);
    }
    for (const auto *key : {"matchedCueTimeSeconds","guideAnticipationSeconds"}) {
      if (!item.hasProperty(key)) continue;
      if (!number(item[key]) || !std::isfinite(static_cast<double>(item[key]))) return false;
      if (juce::String(key)=="matchedCueTimeSeconds") {
        if (!validTime(static_cast<double>(item[key]),result.durationSeconds)) return false;
        suggestion.matchedCueTimeSeconds=static_cast<double>(item[key]);
      } else suggestion.guideAnticipationSeconds=static_cast<double>(item[key]);
    }
    if (suggestion.guideAnticipationSeconds &&
        (!suggestion.matchedCueTimeSeconds ||
         std::abs(suggestion.startSeconds - *suggestion.matchedCueTimeSeconds -
                  *suggestion.guideAnticipationSeconds) > epsilon)) return false;
    previousEnd=suggestion.endSeconds; ids.add(suggestion.id);
    result.suggestions.push_back(std::move(suggestion));
  }
  out=std::move(result); error.clear(); return true;
}

juce::String sectionDetectionCacheKey(const SectionDetectionResult &result,
                                      const FusionOptions &options) {
  auto value=object();
  set(value,"schemaVersion",sectionDetectionSchemaVersion);
  set(value,"algorithm",sectionDetectionAlgorithmVersion);
  set(value,"analysisVersion",result.analysisVersion);
  set(value,"fingerprint",result.analyzedAudioFingerprint);
  set(value,"provider",result.analysisProvider);
  set(value,"providerVersion",result.structure.version);
  set(value,"durationSeconds",result.durationSeconds);
  set(value,"confirmedBars",timesVar(options.confirmedBars));
  set(value,"gridSource",options.confirmedGridSource);
  set(value,"maximumSnapSeconds",options.maximumSnapSeconds);
  set(value,"maximumSnapFractionOfBar",options.maximumSnapFractionOfBar);
  const auto encoded=juce::JSON::toString(value,true);
  return juce::SHA256(encoded.toRawUTF8(),encoded.getNumBytesAsUTF8()).toHexString();
}
} // namespace playworship::analysis
