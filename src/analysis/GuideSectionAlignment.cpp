#include "GuideSectionAlignment.h"
#include "GuideSpeechSegmentation.h"
#include "GuideAcousticTemplateMatcher.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <map>
#include <unordered_set>

namespace playworship::analysis {
namespace {
constexpr double epsilon = 1.0e-7;
// Whisper reports 10 ms ticks after the 20 ms source speech gate. Keep that
// uncertainty separate from epsilon, which only protects floating-point math.
constexpr double onsetBoundaryToleranceSeconds = 0.020;
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
std::vector<GuideGridMark> bars(const GuideAlignmentDraft &draft) {
  std::vector<GuideGridMark> result;
  for (const auto &mark : draft.grid) if (mark.strong) result.push_back(mark);
  return result;
}
struct MusicalOnsetAnchor {
  double seconds;
  bool snapped = false;
};
MusicalOnsetAnchor musicalOnsetAnchor(const std::vector<GuideGridMark> &gridBars,
                                    double seconds) {
  MusicalOnsetAnchor anchor{seconds};
  if (gridBars.empty()) return anchor;
  const auto next = std::lower_bound(gridBars.begin(), gridBars.end(), seconds,
      [](const auto &mark, double time) { return mark.timeSeconds < time; });
  auto nearest = next == gridBars.end() ? std::prev(gridBars.end()) : next;
  if (next != gridBars.begin() &&
      std::abs(std::prev(next)->timeSeconds - seconds) < std::abs(nearest->timeSeconds - seconds))
    nearest = std::prev(next);
  double tolerance = onsetBoundaryToleranceSeconds;
  // Extremely short meters or tempo regions must not make tolerance windows
  // overlap. Musical spacing comes from this grid, never a global BPM formula.
  if (nearest != gridBars.begin())
    tolerance = std::min(tolerance, (nearest->timeSeconds - std::prev(nearest)->timeSeconds) * 0.25);
  if (std::next(nearest) != gridBars.end())
    tolerance = std::min(tolerance, (std::next(nearest)->timeSeconds - nearest->timeSeconds) * 0.25);
  if (std::abs(nearest->timeSeconds - seconds) <= tolerance + epsilon) {
    anchor.seconds = nearest->timeSeconds;
    anchor.snapped = std::abs(anchor.seconds - seconds) > epsilon;
  }
  return anchor;
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
  target.suggestion.sources.addIfNotAlreadyThere(juce::String::fromUTF8(mergedGuideAnnouncementsSource));
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

juce::String segmentIdentityHash(const GuideTranscript &transcript,
                                 const GuideTranscriptSegment &segment,
                                 const juce::String &text) {
  const auto identity = transcript.provider + ":" + transcript.providerVersion + ":" + transcript.audioFingerprint + ":" +
      transcript.modelFingerprint + ":" + juce::String(segment.startSeconds, 12) + ":" + juce::String(segment.endSeconds, 12) + ":" + text;
  return juce::SHA256(identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString().substring(0, 20);
}

bool validConfidence(const std::optional<double> &confidence) {
  return !confidence || (std::isfinite(*confidence) && *confidence >= 0.0 && *confidence <= 1.0);
}

struct TimedCountExpansion {
  bool expanded = false;
  GuideSectionInterpretation section;
  std::vector<GuideSecondaryCueSuggestion> counts;
};

TimedCountExpansion expandTimedCounts(const GuideTranscript &transcript,
    const GuideTranscriptSegment &segment, const std::vector<GuideGridMark> &grid) {
  TimedCountExpansion result;
  const auto compound = guideCompoundCount(segment.text);
  if (!compound || segment.words.size() != size_t(compound->prefixWordCount) + compound->counts.size())
    return result;
  juce::StringArray evidence;
  double previousStart = -1.0;
  for (const auto &word : segment.words) {
    if (!std::isfinite(word.startSeconds) || !std::isfinite(word.endSeconds) ||
        word.startSeconds < segment.startSeconds - epsilon || word.endSeconds > segment.endSeconds + epsilon ||
        word.endSeconds <= word.startSeconds || word.startSeconds <= previousStart ||
        !validConfidence(word.confidence)) return {};
    previousStart = word.startSeconds;
    evidence.add(word.text);
  }
  if (playworship::guide::normalizedWords(evidence.joinIntoString(" ")) !=
      playworship::guide::normalizedWords(segment.text)) return {};
  if (compound->prefixWordCount > 0) {
    auto prefix = segment;
    prefix.text = evidence.joinIntoString(" ", 0, compound->prefixWordCount);
    prefix.alternatives.clear(); prefix.words.clear();
    result.section = interpretGuideSection(prefix);
    if (!result.section.recognized() || result.section.tentative || result.section.ordinal) return {};
    result.section.provenance = "word-interval-counts";
    result.section.reason = juce::String::fromUTF8("La etiqueta y las cuentas tienen evidencia por palabra; cada cuenta se propone sólo en el único pulso contenido en su intervalo ASR.");
  }
  double previousPulse = -1.0;
  for (size_t i = 0; i < compound->counts.size(); ++i) {
    const auto index = size_t(compound->prefixWordCount) + i;
    const auto &word = segment.words[index];
    const auto count = normalizeGuideCountCue(word.startSeconds, word.text, word.confidence);
    if (!count || count->name != juce::String(compound->counts[i]) || !word.confidence ||
        *word.confidence < std::exp(-1.2)) return {};
    auto pulse = std::lower_bound(grid.begin(), grid.end(), word.startSeconds - epsilon,
        [](const auto &mark, double time) { return mark.timeSeconds < time; });
    if (pulse == grid.end() || pulse->timeSeconds >= transcript.durationSeconds - epsilon ||
        pulse->timeSeconds > word.endSeconds + epsilon ||
        (std::next(pulse) != grid.end() && std::next(pulse)->timeSeconds <= word.endSeconds + epsilon) ||
        pulse->timeSeconds <= previousPulse + epsilon) return {};
    previousPulse = pulse->timeSeconds;
    result.counts.push_back({"guide-secondary-" + segmentIdentityHash(transcript, segment, segment.text.trim()) +
        "-word-" + juce::String(int(index)), count->name, word.text,
        word.startSeconds, pulse->timeSeconds, word.confidence, false});
  }
  result.expanded = true;
  return result;
}

bool validDraftAnnotations(const GuideAlignmentDraft &draft, juce::String &error) {
  if (draft.error.isNotEmpty() || !std::isfinite(draft.durationSeconds) ||
      draft.durationSeconds < minimumAcceptedSectionDurationSeconds || draft.durationSeconds > maximumDuration) {
    error = juce::String::fromUTF8("La propuesta tiene una duración inválida o un error pendiente."); return false;
  }
  if (draft.suggestions.size() > maximumSectionSuggestions || draft.secondaryCues.size() > kMaxArrangementCueEvents) {
    error = juce::String::fromUTF8("La propuesta excede el límite de secciones o indicaciones del editor."); return false;
  }
  std::unordered_set<std::string> ids;
  for (const auto &part : draft.suggestions) {
    if (part.id.trim().isEmpty() || !ids.insert(part.id.toStdString()).second ||
        !std::isfinite(part.rawStartSeconds) || part.rawStartSeconds < 0.0 || part.rawStartSeconds > draft.durationSeconds ||
        !std::isfinite(part.startSeconds) || !std::isfinite(part.endSeconds) ||
        part.startSeconds < 0.0 || part.endSeconds > draft.durationSeconds ||
        part.endSeconds - part.startSeconds < minimumAcceptedSectionDurationSeconds ||
        (part.matchedCueTimeSeconds && (!std::isfinite(*part.matchedCueTimeSeconds) ||
            *part.matchedCueTimeSeconds < 0.0 || *part.matchedCueTimeSeconds > draft.durationSeconds))) {
      error = juce::String::fromUTF8("Las secciones propuestas tienen IDs o tiempos inválidos."); return false;
    }
  }
  for (const auto &cue : draft.secondaryCues) {
    if (cue.id.trim().isEmpty() || !ids.insert(cue.id.toStdString()).second ||
        cue.name.trim().isEmpty() || cue.name.length() > 128 ||
        !std::isfinite(cue.rawTimeSeconds) || cue.rawTimeSeconds < 0.0 || cue.rawTimeSeconds >= draft.durationSeconds ||
        !std::isfinite(cue.timeSeconds) || cue.timeSeconds < 0.0 || cue.timeSeconds >= draft.durationSeconds ||
        !validConfidence(cue.asrConfidence)) {
      error = juce::String::fromUTF8("Las indicaciones propuestas tienen IDs, tiempos o nombres inválidos."); return false;
    }
  }
  return true;
}
} // namespace

GuideAlignmentDraft alignGuideSections(const GuideTranscript &transcript, const GuideAlignmentOptions &options) {
  GuideAlignmentDraft draft;
  draft.options = options; draft.durationSeconds = transcript.durationSeconds;
  if (!std::isfinite(draft.durationSeconds) || draft.durationSeconds < minimumAcceptedSectionDurationSeconds ||
      draft.durationSeconds > maximumDuration || !std::isfinite(options.originSeconds) || options.originSeconds < 0 ||
      options.originSeconds > draft.durationSeconds - minimumAcceptedSectionDurationSeconds ||
      transcript.segments.size() > 65536) {
    draft.error = juce::String::fromUTF8("Revisa duración y origen de la guía."); return draft;
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
  for (const auto &segment : transcript.segments) {
    if (!std::isfinite(segment.startSeconds) || !std::isfinite(segment.endSeconds) || segment.startSeconds < 0 ||
        segment.endSeconds <= segment.startSeconds || segment.endSeconds > draft.durationSeconds + epsilon ||
        !validConfidence(segment.confidence)) {
      draft.error = juce::String::fromUTF8("La transcripción contiene tiempos de voz inválidos."); return draft;
    }
  }
  // Acoustic evidence is kept on the unmodified ASR parent, since actual
  // overlapping voices cannot be represented by its nonoverlapping segments.
  // Expand a copy solely for the existing musical/instruction interpretation.
  std::vector<GuideTranscriptSegment> segments;
  std::unordered_set<std::string> acousticSegmentIds;
  for (const auto &parent : transcript.segments) {
    if (segments.size() + std::max(size_t(1), parent.acousticAnnouncements.size()) > 65536) {
      draft.error = juce::String::fromUTF8("La guía contiene demasiados anuncios independientes. No se ha truncado su evidencia.");
      return draft;
    }
    const bool inSource = std::all_of(parent.acousticAnnouncements.begin(),
        parent.acousticAnnouncements.end(), [&](const auto &announcement) {
          return announcement.startSeconds >= 0.0 &&
              announcement.endSeconds <= draft.durationSeconds + epsilon;
        });
    if (!inSource || !guideAcousticAnnouncementsMatchParent(parent)) {
      segments.push_back(parent);
      continue;
    }
    for (const auto &announcement : parent.acousticAnnouncements) {
      GuideTranscriptSegment child;
      child.text = announcement.text;
      child.language = announcement.language;
      child.startSeconds = announcement.startSeconds;
      child.endSeconds = announcement.endSeconds;
      // No token probability exists for a waveform observation. Its measured
      // correlation remains in the parent's separate evidence record.
      acousticSegmentIds.insert(guideSectionSuggestionId(transcript, child).toStdString());
      segments.push_back(std::move(child));
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
  juce::StringArray unavailableEntries;
  std::unordered_set<std::string> secondaryIds;
  for (const auto &segment : segments) {
    const auto text = segment.text.trim();
    if (text.isEmpty()) continue;
    const auto expansion = expandTimedCounts(transcript, segment, draft.grid);
    if (expansion.expanded) {
      for (const auto &count : expansion.counts)
        if (secondaryIds.insert(count.id.toStdString()).second) draft.secondaryCues.push_back(count);
      if (!expansion.section.recognized()) continue;
    }
    auto announcement = interpretGuideAnnouncement(segment, options.learnedMappings);
    if (expansion.expanded) {
      announcement.kind = GuideAnnouncementKind::section;
      announcement.section = expansion.section;
    }
    if (announcement.kind == GuideAnnouncementKind::count ||
        announcement.kind == GuideAnnouncementKind::instruction) {
      const auto id = "guide-secondary-" + segmentIdentityHash(transcript, segment, text);
      // Repeated copies of precisely the same source evidence represent one
      // point. Repeated announcements at different source times remain separate.
      if (secondaryIds.insert(id.toStdString()).second)
        draft.secondaryCues.push_back({id, announcement.secondaryName, segment.text,
            segment.startSeconds, segment.startSeconds, segment.confidence, false});
      continue;
    }
    const auto &interpretation = announcement.section;
    const auto anchor = musicalOnsetAnchor(gridBars, segment.startSeconds);
    const auto nextBar = std::upper_bound(gridBars.begin(), gridBars.end(), anchor.seconds + epsilon,
        [](double time, const auto &mark) { return time < mark.timeSeconds; });
    // -1 represents a spoken cue before the first available musical bar; the
    // following bar is then the origin itself rather than origin + one bar.
    const int containing = int(nextBar - gridBars.begin()) - 1;
    const int requested = containing + 1;
    if (requested < 0 || requested > lastBarIndex) {
      unavailableEntries.add(juce::String::fromUTF8("«") + text + juce::String::fromUTF8("» (") +
                             juce::String(segment.startSeconds, 3) + " s)");
      continue;
    }
    const int target = requested;
    Candidate candidate;
    auto &suggestion = candidate.suggestion;
    suggestion.type = interpretation.type;
    candidate.ordinal = interpretation.ordinal;
    candidate.rawLabels.add(text);
    candidate.identityLabel = interpretation.recognized() ? juce::String(int(interpretation.type)) + ":" + juce::String(candidate.ordinal.value_or(0)) : normalizedWords(text);
    suggestion.displayName = interpretation.recognized()
        ? (interpretation.tentative ? juce::String("Posible ") : juce::String{}) + sectionTypeName(interpretation.type)
        : "Revisar: " + text;
    suggestion.confidenceLevel = interpretation.recognized() && !interpretation.tentative
        ? ConfidenceLevel::medium : ConfidenceLevel::low;
    suggestion.rawStartSeconds = segment.startSeconds;
    suggestion.matchedCueTimeSeconds = segment.startSeconds;
    suggestion.startSeconds = gridBars[size_t(target)].timeSeconds;
    suggestion.startBarIndex = gridBars[size_t(target)].barNumber - 1;
    suggestion.quantizationDistanceSeconds = suggestion.startSeconds - suggestion.rawStartSeconds;
    suggestion.guideAnticipationSeconds = suggestion.quantizationDistanceSeconds;
    suggestion.sources.add(juce::String::fromUTF8("guía"));
    suggestion.sources.add(juce::String::fromUTF8("cuadrícula propuesta"));
    if (acousticSegmentIds.count(guideSectionSuggestionId(transcript, segment).toStdString()))
      suggestion.sources.add("known-guide-recording");
    if (interpretation.provenance.isNotEmpty()) suggestion.sources.add(interpretation.provenance);
    if (interpretation.tentative)
      suggestion.sources.addIfNotAlreadyThere(juce::String::fromUTF8(tentativeGuideInterpretationSource));
    suggestion.reason = juce::String::fromUTF8("Anuncio «") + text + juce::String::fromUTF8("» en ") + juce::String(segment.startSeconds, 6) +
        juce::String::fromUTF8(" s. Entrada propuesta en el compás siguiente al del anuncio (compás ") +
        juce::String(gridBars[size_t(target)].barNumber) + juce::String::fromUTF8("); requiere revisión visual.");
    if (interpretation.reason.isNotEmpty()) suggestion.reason += " " + interpretation.reason;
    if (suggestion.sources.contains("known-guide-recording"))
      suggestion.reason += juce::String::fromUTF8(" Inicio medido por coincidencia acústica con una grabación del pack; la frase original de Whisper se conserva en la transcripción.");
    if (!interpretation.recognized()) suggestion.reason += juce::String::fromUTF8(" El texto no se reconoce como sección; se conserva para corregirlo.");
    if (anchor.snapped) suggestion.reason += juce::String::fromUTF8(" El inicio hablado está junto a una frontera; se ajustó sólo el ancla de cálculo (tolerancia de hasta 20 ms), conservando el tiempo original.");
    if (segment.startSeconds < options.originSeconds - epsilon) {
      suggestion.confidenceLevel = ConfidenceLevel::low;
      suggestion.reason += juce::String::fromUTF8(" El anuncio precede al origen musical; revisa la entrada propuesta.");
    }
    suggestion.id = guideSectionSuggestionId(transcript, segment);
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
    const auto *definition = playworship::guide::findSectionDefinition(suggestion.type);
    // Repeating an unnumbered announcement does not imply a numbered musical
    // variant. Refrain keeps the established review convention from repeated
    // guide cues; an ordinal actually spoken by the guide is retained for any
    // family independently of the catalogue's bundled recording variants.
    const bool inferOrdinal = definition &&
        (definition->numberedVariantLimit > 0 || suggestion.type == SectionType::refrain);
    if (!candidate.conflict && suggestion.type != SectionType::unknown && suggestion.type != SectionType::custom &&
        (candidate.ordinal || (inferOrdinal && totals[suggestion.type] > 1)))
      suggestion.displayName += " " + juce::String(suggestion.occurrence);
    draft.suggestions.push_back(std::move(suggestion));
  }
  if (!unavailableEntries.isEmpty()) {
    draft.error = juce::String::fromUTF8("No hay un compás siguiente válido para ") +
        unavailableEntries.joinIntoString(", ") +
        juce::String::fromUTF8(". Revisa el origen; no se ha desplazado ninguna entrada hacia atrás para hacerla caber. Las demás propuestas siguen visibles.");
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

bool moveGuideSecondaryCue(GuideAlignmentDraft &draft, const juce::String &id,
                            double seconds, bool snapToBeats, juce::String &error) {
  error.clear();
  if (id.isEmpty() || !std::isfinite(seconds) || !validDraftAnnotations(draft, error)) {
    if (error.isEmpty()) error = juce::String::fromUTF8("No se puede mover esta indicación.");
    return false;
  }
  double previousMark = -1.0;
  for (const auto &mark : draft.grid) {
    if (!std::isfinite(mark.timeSeconds) || mark.timeSeconds < 0.0 || mark.timeSeconds > draft.durationSeconds ||
        mark.timeSeconds <= previousMark || mark.barNumber < 1 || mark.pulseNumber < 1) {
      error = juce::String::fromUTF8("La cuadrícula de la propuesta no es válida."); return false;
    }
    previousMark = mark.timeSeconds;
  }
  const auto found = std::find_if(draft.secondaryCues.begin(), draft.secondaryCues.end(),
      [&](const auto &cue) { return cue.id == id; });
  if (found == draft.secondaryCues.end()) {
    error = juce::String::fromUTF8("La indicación propuesta ya no existe."); return false;
  }
  double target = std::clamp(seconds, 0.0, std::nextafter(draft.durationSeconds, 0.0));
  if (snapToBeats) {
    double nearest = std::numeric_limits<double>::quiet_NaN();
    for (const auto &mark : draft.grid) {
      if (mark.timeSeconds >= draft.durationSeconds) continue;
      if (!std::isfinite(nearest) || std::abs(mark.timeSeconds - target) < std::abs(nearest - target) - epsilon ||
          (std::abs(std::abs(mark.timeSeconds - target) - std::abs(nearest - target)) <= epsilon &&
           mark.timeSeconds > nearest)) nearest = mark.timeSeconds;
    }
    if (!std::isfinite(nearest)) {
      error = juce::String::fromUTF8("No hay un pulso válido para ajustar esta indicación."); return false;
    }
    target = nearest;
  }
  if (std::abs(found->timeSeconds - target) > epsilon) {
    found->timeSeconds = target;
    found->userEdited = true;
  }
  return true;
}

SectionAcceptanceResult appendConfirmedGuideSuggestions(const SongArrangementSnapshot &existing,
                                                          const GuideAlignmentDraft &draft) {
  SectionAcceptanceResult rejected;
  rejected.arrangement = existing;
  if (!validDraftAnnotations(draft, rejected.error)) return rejected;
  if (draft.suggestions.empty() && draft.secondaryCues.empty()) {
    rejected.error = juce::String::fromUTF8("No hay sugerencias para confirmar."); return rejected;
  }
  std::unordered_set<std::string> ids;
  const auto addId = [&](const juce::String &id) {
    return id.trim().isNotEmpty() && ids.insert(id.toStdString()).second;
  };
  for (const auto &part : existing.parts) {
    if (!addId(part.id) || !std::isfinite(part.startSec) || !std::isfinite(part.endSec) ||
        part.startSec < 0.0 || part.endSec <= part.startSec || part.endSec > draft.durationSeconds) {
      rejected.error = juce::String::fromUTF8("Las secciones existentes tienen IDs o tiempos inválidos."); return rejected;
    }
  }
  for (const auto &cue : existing.cues) {
    if (!addId(cue.id)) {
      rejected.error = juce::String::fromUTF8("Las indicaciones existentes tienen IDs repetidos o vacíos."); return rejected;
    }
  }
  for (const auto &part : draft.suggestions) {
    if (part.status != SuggestionStatus::rejected && !addId(part.id)) {
      rejected.conflictingSuggestionIds.add(part.id);
      rejected.error = juce::String::fromUTF8("Una sección propuesta tiene un ID ya utilizado."); return rejected;
    }
  }
  for (const auto &cue : draft.secondaryCues) {
    if (!addId(cue.id)) {
      rejected.conflictingSuggestionIds.add(cue.id);
      rejected.error = juce::String::fromUTF8("Una indicación propuesta tiene un ID ya utilizado."); return rejected;
    }
  }
  auto confirmed = draft.suggestions;
  for (auto &suggestion : confirmed) {
    if (!suggestion.sources.contains(juce::String::fromUTF8(tentativeGuideInterpretationSource)) ||
        playworship::guide::findSectionDefinition(suggestion.type) == nullptr) continue;
    const auto canonical = sectionTypeName(suggestion.type);
    const auto numbered = canonical + " " + juce::String(suggestion.occurrence);
    // "Posible" describes the review state, not a native guide recording key.
    // Moving a boundary also sets userEdited, so match the generated name and
    // known family instead; explicit freeform renames have the custom type.
    if (suggestion.displayName == "Posible " + canonical) suggestion.displayName = canonical;
    else if (suggestion.displayName == "Posible " + numbered) suggestion.displayName = numbered;
  }
  auto plan = appendConfirmedSuggestions(existing, confirmed, draft.durationSeconds);
  if (plan.error.isNotEmpty()) return plan;
  auto secondary = draft.secondaryCues;
  std::stable_sort(secondary.begin(), secondary.end(), [](const auto &left, const auto &right) {
    return left.timeSeconds < right.timeSeconds;
  });
  for (const auto &suggestion : secondary) {
    SongArrangementCueSnapshot cue;
    cue.id = suggestion.id; cue.name = suggestion.name.trim(); cue.positionSec = suggestion.timeSeconds;
    auto normalized = normalizeGuideCountCue(cue.positionSec, cue.name, suggestion.asrConfidence);
    if (!normalized) normalized = normalizeGuideUtilityCue(cue.positionSec, cue.name, suggestion.asrConfidence);
    if (normalized) cue.name = normalized->name;
    cue.color = (isGuideCountCue(cue.name) ? juce::Colour::fromRGB(91, 169, 218)
                                         : juce::Colour::fromRGB(224, 171, 91)).toString();
    const SongArrangementPartSnapshot *parent = nullptr;
    for (const auto &part : plan.arrangement.parts) {
      if (cue.positionSec < part.startSec || cue.positionSec >= part.endSec) continue;
      if (parent != nullptr) {
        rejected.error = juce::String::fromUTF8("La indicación coincide con varias secciones superpuestas."); return rejected;
      }
      parent = &part;
    }
    if (parent != nullptr) {
      cue.partId = parent->id;
      cue.offsetSec = cue.positionSec - parent->startSec;
    }
    plan.arrangement.cues.push_back(std::move(cue));
    plan.addedSuggestionIds.add(suggestion.id);
  }
  if (arrangementCueEventCount(plan.arrangement) > kMaxArrangementCueEvents) {
    rejected.error = juce::String::fromUTF8("La confirmación excedería el límite de 512 anuncios. No se aplicó ninguna sugerencia.");
    return rejected;
  }
  std::stable_sort(plan.arrangement.cues.begin(), plan.arrangement.cues.end(), [&](const auto &left, const auto &right) {
    return resolveArrangementCueSeconds(plan.arrangement, left) < resolveArrangementCueSeconds(plan.arrangement, right);
  });
  return plan;
}
} // namespace playworship::analysis
