#include "GuideSectionAlignment.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace playworship::analysis {
namespace {
constexpr double epsilon = 1.0e-7;
constexpr size_t maximumGridMarks = 250000;
constexpr double maximumDuration = 12.0 * 3600.0;
struct Rhythm { double bpm; int numerator, denominator; };

bool rhythm(double bpm, const juce::String &meter, Rhythm &out) {
  const auto text = meter.trim();
  const auto slash = text.indexOfChar('/');
  if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 300.0 || slash <= 0 ||
      text.indexOfChar(slash + 1, '/') >= 0) return false;
  const auto numerator = text.substring(0, slash).trim();
  const auto denominator = text.substring(slash + 1).trim();
  if (numerator.isEmpty() || denominator.isEmpty() ||
      !numerator.containsOnly("0123456789") || !denominator.containsOnly("0123456789") ||
      numerator.length() > 2 || denominator.length() > 2) return false;
  const auto n = numerator.getIntValue(), d = denominator.getIntValue();
  if (n < 1 || n > 64 || d < 1 || d > 64) return false;
  out = {bpm, n, d}; return true;
}

bool buildGrid(GuideAlignmentDraft &draft) {
  Rhythm base{};
  if (!rhythm(draft.options.bpm, draft.options.timeSignature, base)) {
    draft.error = juce::String::fromUTF8("Revisa el BPM original (20–300) y el compás de la cuadrícula."); return false;
  }
  double previousRegionEnd = 0;
  for (const auto &region : draft.options.tempoMap.regions) {
    Rhythm current{};
    if (!std::isfinite(region.startSec) || !std::isfinite(region.endSec) ||
        region.startSec < 0 || region.endSec <= region.startSec || region.startSec < previousRegionEnd - epsilon ||
        !rhythm(region.bpm, region.timeSignature.trim().isEmpty() ? draft.options.timeSignature : region.timeSignature, current)) {
      draft.error = juce::String::fromUTF8("El mapa de tempo contiene regiones inválidas, desordenadas o superpuestas."); return false;
    }
    previousRegionEnd = region.endSec;
  }
  double phaseQuarters = 0;
  int barNumber = 1;
  Rhythm previous = base;
  const auto append = [&](double start, double end, Rhythm current) -> bool {
    if (end <= start) return true;
    // Same musical phase contract as PracticeTimelineRuler, without a visual
    // width, density reduction or its 4096-mark rendering limit.
    if (previous.numerator != current.numerator || previous.denominator != current.denominator) {
      if (phaseQuarters > epsilon) ++barNumber;
      phaseQuarters = 0;
    }
    const double quarterSeconds = 60.0 / current.bpm;
    const double quartersPerBar = current.numerator * 4.0 / current.denominator;
    const double pulseSeconds = quarterSeconds * 4.0 / current.denominator;
    const double barOrigin = start - phaseQuarters * quarterSeconds;
    const auto first = static_cast<long long>(std::max(0.0, std::ceil((start - barOrigin) / pulseSeconds - epsilon)));
    const auto last = static_cast<long long>(std::floor((end - barOrigin) / pulseSeconds + epsilon));
    for (auto pulse = first; pulse <= last; ++pulse) {
      const double time = barOrigin + double(pulse) * pulseSeconds;
      if (time < start - epsilon || time > end + epsilon ||
          (end < draft.durationSeconds - epsilon && time >= end - epsilon)) continue;
      if (draft.grid.size() >= maximumGridMarks) {
        draft.error = juce::String::fromUTF8("La cuadrícula excede 250000 marcas. Reduce la duración o la subdivisión del compás; no se ha truncado el resultado.");
        return false;
      }
      const auto pulseNumber = int(pulse % current.numerator) + 1;
      draft.grid.push_back({std::clamp(time, start, end), barNumber + int(pulse / current.numerator), pulseNumber, pulseNumber == 1});
    }
    const double total = phaseQuarters + (end - start) / quarterSeconds;
    const auto completed = int(std::floor(total / quartersPerBar + epsilon));
    barNumber += completed;
    phaseQuarters = std::max(0.0, total - completed * quartersPerBar);
    previous = current;
    return true;
  };
  double cursor = draft.options.originSeconds;
  for (const auto &region : draft.options.tempoMap.regions) {
    if (region.endSec <= cursor || region.startSec >= draft.durationSeconds) continue;
    const double start = std::clamp(region.startSec, cursor, draft.durationSeconds);
    if (start > cursor && !append(cursor, start, base)) return false;
    const double end = std::clamp(region.endSec, start, draft.durationSeconds);
    Rhythm current{};
    rhythm(region.bpm, region.timeSignature.trim().isEmpty() ? draft.options.timeSignature : region.timeSignature, current);
    if (!append(start, end, current)) return false;
    cursor = end;
    if (cursor >= draft.durationSeconds) break;
  }
  return cursor >= draft.durationSeconds || append(cursor, draft.durationSeconds, base);
}

juce::String normalizedWords(juce::String text) {
  text = text.toLowerCase();
  for (const auto *pair : {"áa", "ée", "íi", "óo", "úu", "üu"}) {
    const auto characters = juce::String::fromUTF8(pair);
    text = text.replaceCharacter(characters[0], characters[1]);
  }
  juce::String words;
  for (auto character : text)
    words += juce::CharacterFunctions::isLetterOrDigit(character) ? character : static_cast<juce::juce_wchar>(' ');
  auto tokens = juce::StringArray::fromTokens(words, " ", "");
  tokens.removeEmptyStrings(); return tokens.joinIntoString(" ");
}
bool pureCount(const juce::String &text) {
  const auto tokens = juce::StringArray::fromTokens(normalizedWords(text), " ", "");
  const juce::StringArray counts{"cero","un","uno","una","dos","tres","cuatro","cinco","seis","siete","ocho","nueve","diez",
      "once","doce","trece","catorce","quince","dieciseis","zero","one","two","three","four","five","six","seven","eight","nine","ten"};
  const juce::StringArray connectors{"cuenta","conteo","count","in","y","and"};
  bool hasNumber = false;
  for (const auto &token : tokens) {
    if (token.isEmpty()) continue;
    if (counts.contains(token) || token.containsOnly("0123456789")) hasNumber = true;
    else if (!connectors.contains(token)) return false;
  }
  return hasNumber;
}
std::vector<GuideGridMark> bars(const GuideAlignmentDraft &draft) {
  std::vector<GuideGridMark> result;
  for (const auto &mark : draft.grid) if (mark.strong) result.push_back(mark);
  return result;
}
std::optional<int> exactBar(const std::vector<GuideGridMark> &gridBars, double seconds) {
  const auto found = std::lower_bound(gridBars.begin(), gridBars.end(), seconds - epsilon,
      [](const auto &mark, double time) { return mark.timeSeconds < time; });
  if (found != gridBars.end() && std::abs(found->timeSeconds - seconds) <= epsilon) return found->barNumber - 1;
  return {};
}

struct Candidate {
  SectionSuggestion suggestion;
  std::optional<int> ordinal;
  juce::String identityLabel;
  juce::StringArray rawLabels;
  bool conflict = false;
};
void merge(Candidate &target, const Candidate &incoming, bool nearBoundary) {
  for (const auto &label : incoming.rawLabels) if (!target.rawLabels.contains(label)) target.rawLabels.add(label);
  target.suggestion.reason += " " + incoming.suggestion.reason;
  target.conflict = target.conflict || incoming.conflict || target.identityLabel != incoming.identityLabel || nearBoundary;
  if (target.conflict) {
    target.suggestion.type = SectionType::unknown;
    target.ordinal.reset();
    target.suggestion.displayName = "Revisar: " + target.rawLabels.joinIntoString(" / ");
    target.suggestion.confidenceLevel = ConfidenceLevel::low;
    target.suggestion.reason += nearBoundary
        ? juce::String::fromUTF8(" Los límites están demasiado próximos; se conserva un único bloque para revisar ambos anuncios.")
        : juce::String::fromUTF8(" Hay anuncios diferentes para la misma entrada; revisa el nombre y el límite.");
  } else {
    target.suggestion.reason += juce::String::fromUTF8(" Anuncios coincidentes fusionados sin añadir otra sección.");
  }
}
} // namespace

GuideAlignmentDraft alignGuideSections(const GuideTranscript &transcript, const GuideAlignmentOptions &options) {
  GuideAlignmentDraft draft;
  draft.options = options; draft.durationSeconds = transcript.durationSeconds;
  if (!std::isfinite(draft.durationSeconds) || draft.durationSeconds < minimumAcceptedSectionDurationSeconds ||
      draft.durationSeconds > maximumDuration || !std::isfinite(options.originSeconds) || options.originSeconds < 0 ||
      options.originSeconds > draft.durationSeconds - minimumAcceptedSectionDurationSeconds ||
      options.anticipationBars < 0 || options.anticipationBars > 2 || transcript.segments.size() > 65536) {
    draft.error = juce::String::fromUTF8("Revisa duración, origen y anticipación (0, 1 o 2 compases) de la guía."); return draft;
  }
  if (!buildGrid(draft)) { draft.grid.clear(); return draft; }
  const auto gridBars = bars(draft);
  if (gridBars.empty()) { draft.error = juce::String::fromUTF8("No hay compases válidos desde el origen propuesto."); return draft; }
  auto latestStart = draft.durationSeconds - minimumAcceptedSectionDurationSeconds;
  if (draft.durationSeconds - latestStart < minimumAcceptedSectionDurationSeconds)
    latestStart = std::nextafter(latestStart, -std::numeric_limits<double>::infinity());
  const auto lastUsable = std::upper_bound(gridBars.begin(), gridBars.end(), latestStart,
      [](double time, const auto &mark) { return time < mark.timeSeconds; });
  const auto lastBarIndex = int(lastUsable - gridBars.begin()) - 1;
  if (lastBarIndex < 0) { draft.error = juce::String::fromUTF8("El origen no deja una sección de al menos 0,25 s."); return draft; }
  auto segments = transcript.segments;
  for (const auto &segment : segments) {
    if (!std::isfinite(segment.startSeconds) || !std::isfinite(segment.endSeconds) || segment.startSeconds < 0 ||
        segment.endSeconds <= segment.startSeconds || segment.endSeconds > draft.durationSeconds + epsilon) {
      draft.error = juce::String::fromUTF8("La transcripción contiene tiempos de voz inválidos."); return draft;
    }
  }
  std::stable_sort(segments.begin(), segments.end(), [](const auto &a, const auto &b) {
    if (a.startSeconds < b.startSeconds) return true;
    if (b.startSeconds < a.startSeconds) return false;
    if (a.endSeconds < b.endSeconds) return true;
    if (b.endSeconds < a.endSeconds) return false;
    return a.text < b.text;
  });
  std::vector<Candidate> candidates;
  for (const auto &segment : segments) {
    const auto text = segment.text.trim();
    if (text.isEmpty() || pureCount(text)) continue;
    const auto cue = normalizeGuideCue(segment.startSeconds, text);
    const auto nextBar = std::upper_bound(gridBars.begin(), gridBars.end(), segment.startSeconds + epsilon,
        [](double time, const auto &mark) { return time < mark.timeSeconds; });
    const int containing = std::max(0, int(nextBar - gridBars.begin()) - 1);
    const int requested = containing + options.anticipationBars;
    const int target = std::clamp(requested, 0, lastBarIndex);
    Candidate candidate;
    auto &suggestion = candidate.suggestion;
    suggestion.type = cue ? cue->normalizedType : SectionType::unknown;
    candidate.ordinal = cue ? cue->spokenOrdinal : std::optional<int>{};
    candidate.rawLabels.add(text);
    candidate.identityLabel = cue ? juce::String(int(cue->normalizedType)) + ":" + juce::String(candidate.ordinal.value_or(0)) : normalizedWords(text);
    suggestion.displayName = cue ? sectionTypeName(cue->normalizedType) : "Revisar: " + text;
    suggestion.confidenceLevel = cue ? ConfidenceLevel::medium : ConfidenceLevel::low;
    suggestion.rawStartSeconds = segment.startSeconds;
    suggestion.matchedCueTimeSeconds = segment.startSeconds;
    suggestion.startSeconds = gridBars[size_t(target)].timeSeconds;
    suggestion.startBarIndex = gridBars[size_t(target)].barNumber - 1;
    suggestion.quantizationDistanceSeconds = suggestion.startSeconds - suggestion.rawStartSeconds;
    suggestion.guideAnticipationSeconds = suggestion.quantizationDistanceSeconds;
    suggestion.sources.add(juce::String::fromUTF8("guía"));
    suggestion.sources.add(juce::String::fromUTF8("cuadrícula propuesta"));
    suggestion.reason = juce::String::fromUTF8("Anuncio «") + text + juce::String::fromUTF8("» en ") + juce::String(segment.startSeconds, 6) +
        juce::String::fromUTF8(" s. Entrada propuesta con ") + juce::String(options.anticipationBars) + juce::String::fromUTF8(" compás(es) de anticipación; requiere revisión visual.");
    if (!cue) suggestion.reason += juce::String::fromUTF8(" El texto no se reconoce como sección; se conserva para corregirlo.");
    if (target != requested || segment.startSeconds < options.originSeconds - epsilon) {
      suggestion.confidenceLevel = ConfidenceLevel::low;
      suggestion.reason += juce::String::fromUTF8(" Entrada limitada al rango disponible de la guía.");
    }
    const auto identity = transcript.provider + ":" + transcript.providerVersion + ":" + transcript.audioFingerprint + ":" +
        transcript.modelFingerprint + ":" + juce::String(segment.startSeconds, 12) + ":" + juce::String(segment.endSeconds, 12) + ":" + text;
    suggestion.id = "guide-" + juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString().substring(0, 20);
    if (!candidates.empty() && suggestion.startSeconds - candidates.back().suggestion.startSeconds < minimumAcceptedSectionDurationSeconds) {
      const bool near = std::abs(suggestion.startSeconds - candidates.back().suggestion.startSeconds) > epsilon;
      merge(candidates.back(), candidate, near);
    } else candidates.push_back(std::move(candidate));
  }
  if (candidates.size() > maximumSectionSuggestions) {
    draft.error = juce::String::fromUTF8("La guía excede el límite de 256 secciones del editor. No se ha aplicado ninguna."); return draft;
  }
  std::map<SectionType, int> totals, occurrences;
  for (const auto &candidate : candidates) ++totals[candidate.suggestion.type];
  for (size_t i = 0; i < candidates.size(); ++i) {
    auto &candidate = candidates[i]; auto &suggestion = candidate.suggestion;
    suggestion.endSeconds = i + 1 < candidates.size() ? candidates[i + 1].suggestion.startSeconds : draft.durationSeconds;
    suggestion.endBarIndex = exactBar(gridBars, suggestion.endSeconds);
    const int occurrence = ++occurrences[suggestion.type];
    suggestion.occurrence = candidate.ordinal.value_or(occurrence);
    if (!candidate.conflict && suggestion.type != SectionType::unknown && suggestion.type != SectionType::custom &&
        (candidate.ordinal || totals[suggestion.type] > 1)) suggestion.displayName += " " + juce::String(suggestion.occurrence);
    draft.suggestions.push_back(std::move(suggestion));
  }
  return draft;
}

bool moveGuideSectionBoundary(GuideAlignmentDraft &draft, const juce::String &id, double seconds, bool snapToBars, juce::String &error) {
  error.clear();
  if (draft.error.isNotEmpty() || id.isEmpty() || !std::isfinite(seconds) || !std::isfinite(draft.durationSeconds) || draft.durationSeconds <= 0) {
    error = juce::String::fromUTF8("No se puede mover este límite de la propuesta."); return false;
  }
  size_t selected = draft.suggestions.size();
  juce::StringArray identifiers;
  double previousMark = -1;
  for (const auto &mark : draft.grid) {
    if (!std::isfinite(mark.timeSeconds) || mark.timeSeconds < 0 || mark.timeSeconds > draft.durationSeconds + epsilon ||
        mark.timeSeconds <= previousMark || mark.barNumber < 1 || mark.pulseNumber < 1) {
      error = juce::String::fromUTF8("La cuadrícula de la propuesta no es válida."); return false;
    }
    previousMark = mark.timeSeconds;
  }
  for (size_t i = 0; i < draft.suggestions.size(); ++i) {
    const auto &suggestion = draft.suggestions[i];
    if (suggestion.id.isEmpty() || identifiers.contains(suggestion.id) || !std::isfinite(suggestion.rawStartSeconds) ||
        (suggestion.matchedCueTimeSeconds && !std::isfinite(*suggestion.matchedCueTimeSeconds)) ||
        !std::isfinite(suggestion.startSeconds) || !std::isfinite(suggestion.endSeconds) || suggestion.startSeconds < 0 ||
        suggestion.endSeconds > draft.durationSeconds + epsilon || suggestion.endSeconds - suggestion.startSeconds < minimumAcceptedSectionDurationSeconds - epsilon ||
        (i > 0 && std::abs(draft.suggestions[i - 1].endSeconds - suggestion.startSeconds) > epsilon)) {
      error = juce::String::fromUTF8("Las propuestas tienen límites inválidos o no son contiguas."); return false;
    }
    identifiers.add(suggestion.id);
    if (suggestion.id == id) {
      if (selected != draft.suggestions.size()) { error = "ID de propuesta duplicado."; return false; }
      selected = i;
    }
  }
  if (selected == draft.suggestions.size()) { error = juce::String::fromUTF8("La propuesta ya no existe."); return false; }
  double minimum = selected == 0 ? 0.0 : draft.suggestions[selected - 1].startSeconds + minimumAcceptedSectionDurationSeconds;
  double maximum = draft.suggestions[selected].endSeconds - minimumAcceptedSectionDurationSeconds;
  if (selected > 0 && minimum - draft.suggestions[selected - 1].startSeconds < minimumAcceptedSectionDurationSeconds)
    minimum = std::nextafter(minimum, std::numeric_limits<double>::infinity());
  if (draft.suggestions[selected].endSeconds - maximum < minimumAcceptedSectionDurationSeconds)
    maximum = std::nextafter(maximum, -std::numeric_limits<double>::infinity());
  if (minimum > maximum) { error = juce::String::fromUTF8("No hay espacio para mover el límite."); return false; }
  double target = std::clamp(seconds, minimum, maximum);
  const auto gridBars = bars(draft);
  if (snapToBars) {
    double nearest = std::numeric_limits<double>::quiet_NaN();
    for (const auto &bar : gridBars) {
      if (!std::isfinite(bar.timeSeconds) || bar.timeSeconds < minimum - epsilon || bar.timeSeconds > maximum + epsilon) continue;
      if (!std::isfinite(nearest) || std::abs(bar.timeSeconds - target) < std::abs(nearest - target) - epsilon ||
          (std::abs(std::abs(bar.timeSeconds - target) - std::abs(nearest - target)) <= epsilon && bar.timeSeconds > nearest)) nearest = bar.timeSeconds;
    }
    if (!std::isfinite(nearest)) { error = juce::String::fromUTF8("No hay un compás válido entre los límites vecinos."); return false; }
    target = std::clamp(nearest, minimum, maximum);
  }
  auto &suggestion = draft.suggestions[selected];
  if (std::abs(suggestion.startSeconds - target) <= epsilon) return true;
  suggestion.startSeconds = target; suggestion.startBarIndex = exactBar(gridBars, target);
  suggestion.quantizationDistanceSeconds = target - suggestion.rawStartSeconds;
  if (suggestion.matchedCueTimeSeconds) suggestion.guideAnticipationSeconds = target - *suggestion.matchedCueTimeSeconds;
  suggestion.userEdited = true;
  suggestion.reason += juce::String::fromUTF8(" Inicio ajustado manualmente durante la revisión.");
  if (selected > 0) {
    auto &previous = draft.suggestions[selected - 1];
    previous.endSeconds = target; previous.endBarIndex = suggestion.startBarIndex;
    previous.userEdited = true;
  }
  return true;
}
} // namespace playworship::analysis
