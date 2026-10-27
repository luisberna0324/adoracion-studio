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
  value = value.toLowerCase();
  for (const auto *pair : {"áa", "ée", "íi", "óo", "úu", "üu"}) {
    const auto chars = juce::String::fromUTF8(pair);
    value = value.replaceCharacter(chars[0], chars[1]);
  }
  juce::String output;
  for (auto character : value)
    output += juce::CharacterFunctions::isLetterOrDigit(character)
                  ? character : static_cast<juce::juce_wchar>(' ');
  auto tokens = juce::StringArray::fromTokens(output, " ", "");
  tokens.removeEmptyStrings();
  return tokens.joinIntoString(" ");
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
  switch (type) {
  case SectionType::intro: return "Intro";
  case SectionType::verse: return "Verso";
  case SectionType::preChorus: return "Pre-coro";
  case SectionType::chorus: return "Coro";
  case SectionType::bridge: return "Puente";
  case SectionType::instrumental: return "Instrumental";
  case SectionType::outro: return "Salida";
  case SectionType::ending: return "Final";
  case SectionType::custom: return "Personalizada";
  case SectionType::unknown: return juce::String::fromUTF8("Sección sugerida");
  }
  return juce::String::fromUTF8("Sección sugerida");
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
  auto tokens = juce::StringArray::fromTokens(text, " ", "");
  if (tokens.isEmpty()) return {};
  GuideCue cue;
  cue.cueTimeSeconds = time;
  cue.rawText = raw;
  cue.asrConfidence = confidence;
  // Long unconstrained text is not treated as a directive; the ASR provider must
  // supply timed utterances rather than an entire song's transcription.
  if (tokens.size() > 10) return {};
  int nameIndex = -1;
  for (int i = 0; i < tokens.size(); ++i) {
    const auto &token = tokens[i];
    if (token == "intro") cue.normalizedType = SectionType::intro;
    else if (token == "verso" || token == "estrofa") cue.normalizedType = SectionType::verse;
    else if (token == "precoro" || (token == "pre" && i + 1 < tokens.size() && tokens[i + 1] == "coro")) {
      cue.normalizedType = SectionType::preChorus;
      if (token == "pre") ++i;
    } else if (token == "coro") cue.normalizedType = SectionType::chorus;
    else if (token == "puente") cue.normalizedType = SectionType::bridge;
    else if (token == "instrumental" || token == "solo" || token == "interludio")
      cue.normalizedType = SectionType::instrumental;
    else if (token == "salida" || token == "outro") cue.normalizedType = SectionType::outro;
    else if (token == "final") cue.normalizedType = SectionType::ending;
    else continue;
    nameIndex = i;
    break;
  }
  if (nameIndex < 0) return {};
  static const std::map<juce::String, int> ordinals{
      {"uno",1},{"un",1},{"una",1},{"primero",1},{"primera",1},
      {"dos",2},{"segundo",2},{"segunda",2},{"tres",3},{"tercero",3},{"tercera",3},
      {"cuatro",4},{"cuarto",4},{"cuarta",4},{"cinco",5},{"quinto",5},{"quinta",5},
      {"seis",6},{"sexto",6},{"sexta",6},{"siete",7},{"septimo",7},{"septima",7},
      {"ocho",8},{"octavo",8},{"octava",8},{"nueve",9},{"noveno",9},{"novena",9},
      {"diez",10},{"decimo",10},{"decima",10}};
  for (int i = juce::jmax(0, nameIndex - 1); i < tokens.size(); ++i) {
    const auto found = ordinals.find(tokens[i]);
    if (found != ordinals.end()) { cue.spokenOrdinal = found->second; break; }
    if (tokens[i].containsOnly("0123456789")) {
      const auto ordinal = tokens[i].getIntValue();
      if (ordinal > 0 && ordinal <= 999) { cue.spokenOrdinal = ordinal; break; }
    }
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
        !readEnum(item["normalizedType"],static_cast<int>(SectionType::custom),cue.normalizedType)) return false;
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
        !readEnum(item["type"],static_cast<int>(SectionType::custom),suggestion.type) ||
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
