#include "GuideAcousticTemplateMatcher.h"
#include "GuideRepetitionMatcher.h"
#include "GuideSpeechSegmentation.h"
#include "GuideSectionInterpretation.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>

namespace playworship::analysis {
namespace {
constexpr double rate = 16000.0;
constexpr size_t maximumTemplates = 256, maximumSamples = 21 * 16000;
constexpr size_t maximumCandidates = 8;
constexpr double minimumCorrelation = .68;

bool cancelledNow(const CancellationCheck &cancelled) { return cancelled && cancelled(); }

// Explicit local developer opt-in; numerical diagnostics only. No transcript,
// path, recording identity or PCM is logged by this diagnostic channel.
void diagnostic(int stage, double a = 0, double b = 0, double c = 0,
                double d = 0, double e = 0, double f = 0) {
  static const bool enabled = [] {
    const auto *value = std::getenv("PLAYWORSHIP_GUIDE_ACOUSTIC_DIAGNOSTICS");
    return value && std::string(value) == "1";
  }();
  if (enabled) std::fprintf(stderr, "GUIDE_ACOUSTIC_DIAG %d %.9g %.9g %.9g %.9g %.9g %.9g\n",
                            stage, a, b, c, d, e, f);
}

juce::String meaning(const juce::String &text) {
  if (const auto count = normalizeGuideCountCue(0.0, text))
    if (count->name.length() == 1 && count->name[0] >= '1' && count->name[0] <= '7')
      return "count:" + count->name;
  if (playworship::guide::isKnownUtilityInstruction(text))
    if (const auto cue = normalizeGuideUtilityCue(0.0, text))
      return "instruction:" + playworship::guide::normalizedWords(cue->name);
  if (isExactGuideSectionDirective(text))
    if (const auto cue = normalizeGuideCue(0.0, text))
      return "section:" + juce::String(int(cue->normalizedType)) + ":" +
          juce::String(cue->spokenOrdinal.value_or(0));
  // Interpretation validates its segment contract even when only classifying
  // a filename/atomic ASR phrase. Supply a finite neutral interval here.
  GuideTranscriptSegment candidate; candidate.text = text; candidate.endSeconds = 1.0;
  const auto repaired = interpretGuideAnnouncement(candidate);
  if (repaired.kind == GuideAnnouncementKind::instruction && repaired.provenance == "confirmed-corpus-alias")
    return "instruction:" + playworship::guide::normalizedWords(repaired.secondaryName);
  return {};
}

// At least two literal atomic pieces, containing a count. An unnumbered
// section can occur among counts (Whisper may merge the preceding "two").
// Any unknown word, narration or numbered section rejects the count mode.
std::optional<juce::StringArray> countPhrases(const juce::String &text) {
  const auto words = juce::StringArray::fromTokens(playworship::guide::normalizedWords(text), " ", "");
  if (words.size() < 2 || words.size() > 16) return {};
  juce::StringArray result;
  int counts = 0, sections = 0;
  for (int index = 0; index < words.size();) {
    const auto key = meaning(words[index]);
    if (key.startsWith("count:")) {
      result.add(words[index++]); ++counts;
    } else {
      int matched = 0;
      for (int end = words.size(); end > index; --end) {
        const auto phrase = words.joinIntoString(" ", index, end - index);
        const auto *definition = playworship::guide::findSectionDefinition(phrase);
        if (definition && definition->numberedVariantLimit == 0) {
          if (++sections > 1) return {};
          result.add(phrase); matched = end - index; break;
        }
      }
      if (matched == 0) return {};
      index += matched;
    }
    if (result.size() > 8) return {};
  }
  if (result.size() < 2 || counts == 0 || (counts < 2 && sections == 0)) return {};
  return result;
}

std::optional<juce::StringArray> hintPhrases(const juce::String &text) {
  if (text.length() > 512) return {};
  if (const auto counts = countPhrases(text)) return counts;
  if (const auto compound = guideCompoundDirectives(text)) return compound->phrases;
  // An atomic ordinal or complete utility must not be reinterpreted by the
  // generic multi-role parser ("Verse 2" is not "Verse" plus a count).
  if (meaning(text).isNotEmpty()) return {};
  const auto words = juce::StringArray::fromTokens(playworship::guide::normalizedWords(text), " ", "");
  if (words.size() < 2 || words.size() > 32) return {};
  juce::StringArray result;
  size_t nodes = 0;
  const std::function<bool(int, int)> parse = [&](int index, int sections) {
    if (++nodes > 256) return false;
    if (index == words.size()) return result.size() >= 2;
    if (result.size() >= 8) return false;
    // ASR may insert an isolated conjunction between two exact guide parts.
    if (!result.isEmpty() && (words[index] == "o" || words[index] == "y" ||
        words[index] == "and" || words[index] == "or")) return parse(index + 1, sections);
    for (int end = words.size(); end > index; --end) {
      const auto phrase = words.joinIntoString(" ", index, end - index);
      const auto key = meaning(phrase);
      if (key.isEmpty() || (key.startsWith("section:") && sections != 0)) continue;
      result.add(phrase);
      if (parse(end, sections + int(key.startsWith("section:")))) return true;
      result.remove(result.size() - 1);
    }
    return false;
  };
  if (parse(0, 0)) return result;
  return {};
}

bool sequenceMatches(const juce::String &text,
                     const std::vector<GuideAcousticAnnouncement> &announcements) {
  const auto hints = hintPhrases(text); if (!hints) return false;
  bool counts = false, utilities = false;
  for (const auto &hint : *hints) {
    counts = counts || meaning(hint).startsWith("count:");
    utilities = utilities || meaning(hint).startsWith("instruction:");
  }
  size_t observed = 0;
  for (const auto &announcement : announcements) {
    const auto actual = meaning(announcement.text);
    if (actual.isEmpty()) return false;
    if (observed < size_t(hints->size()) && actual == meaning((*hints)[int(observed)])) ++observed;
    else if (!(counts && actual.startsWith("count:")) &&
             !(utilities && actual.startsWith("instruction:"))) return false;
  }
  return observed == size_t(hints->size());
}

juce::String filenameLabel(const juce::String &name) {
  const auto stem = name.upToLastOccurrenceOf(".", false, false);
  juce::StringArray labels;
  labels.add(stem.upToFirstOccurrenceOf("(", false, false).trim());
  int start = 0;
  while ((start = stem.indexOfChar(start, '(')) >= 0) {
    const int close = stem.indexOfChar(start + 1, ')');
    if (close < 0) return {};
    labels.add(stem.substring(start + 1, close).trim()); start = close + 1;
  }
  juce::String selected, key;
  for (const auto &label : labels) {
    const auto candidate = meaning(label);
    if (candidate.isEmpty()) continue;
    if (key.isNotEmpty() && key != candidate) return {};
    key = candidate;
    if (selected.isEmpty()) selected = label;
  }
  return selected;
}

std::vector<float> resampleTemplate(juce::AudioFormatReader &reader,
                                   const CancellationCheck &cancelled) {
  if (!std::isfinite(reader.sampleRate) || reader.sampleRate < 8000 ||
      reader.sampleRate > 384000 || reader.numChannels == 0 || reader.numChannels > 16 ||
      reader.lengthInSamples <= 0 || double(reader.lengthInSamples) / reader.sampleRate > 3.0) return {};
  juce::AudioBuffer<float> decoded(int(reader.numChannels), int(reader.lengthInSamples));
  if (!reader.read(&decoded, 0, decoded.getNumSamples(), 0, true, true)) return {};
  std::vector<float> mono(size_t(decoded.getNumSamples()));
  for (size_t i = 0; i < mono.size(); ++i) {
    if ((i & 4095) == 0 && cancelledNow(cancelled)) return {};
    double value = 0.0;
    for (int channel = 0; channel < decoded.getNumChannels(); ++channel) {
      const float sample = decoded.getSample(channel, int(i));
      if (!std::isfinite(sample)) return {};
      value += sample;
    }
    mono[i] = float(value / decoded.getNumChannels());
  }
  const double ratio = reader.sampleRate / rate;
  const double cutoff = .475 * std::min(1.0, 1.0 / ratio);
  constexpr int radius = 24;
  constexpr double pi = 3.14159265358979323846;
  std::vector<float> result(size_t(std::ceil(double(mono.size()) / ratio)));
  for (size_t i = 0; i < result.size(); ++i) {
    if ((i & 255) == 0 && cancelledNow(cancelled)) return {};
    const double absolute = double(i) * ratio;
    const auto centre = static_cast<juce::int64>(std::floor(absolute));
    double value = 0.0, weights = 0.0;
    for (int tap = -radius + 1; tap <= radius; ++tap) {
      const auto index = centre + tap;
      const double distance = absolute - double(index), argument = 2.0 * cutoff * distance;
      const double sinc = std::abs(argument) < 1e-12 ? 1.0 : std::sin(pi * argument) / (pi * argument);
      const double window = .5 + .5 * std::cos(pi * distance / radius);
      const double weight = 2.0 * cutoff * sinc * window;
      if (index >= 0 && index < juce::int64(mono.size())) value += mono[size_t(index)] * weight;
      weights += weight;
    }
    result[i] = float(weights != 0.0 ? value / weights : 0.0);
    if (!std::isfinite(result[i])) return {};
  }
  size_t first = result.size(), last = 0;
  for (size_t begin = 0; begin < result.size(); begin += 320) {
    const auto end = std::min(result.size(), begin + 320);
    double energy = 0.0; float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) {
      energy += double(result[i]) * result[i]; peak = std::max(peak, std::abs(result[i]));
    }
    if (std::sqrt(energy / double(end - begin)) >= .0025 && peak >= .006f) {
      first = std::min(first, begin); last = end;
    }
  }
  if (last <= first || last - first < 1280) return {};
  return {result.begin() + std::ptrdiff_t(first), result.begin() + std::ptrdiff_t(last)};
}

int correlationOrder(size_t source, size_t maximumPattern) {
  size_t length = 1; int order = 0;
  while (length < source + std::min(source, maximumPattern) - 1) { length <<= 1; ++order; }
  return order;
}

// Full-rate FFT correlation considers every source-sample phase. Averaging to
// a 2 kHz waveform before matching can alias pitch into another period or lose
// the true peak entirely; silence/energy and final direct-NCC guards remain.
class SourceCorrelations {
public:
  SourceCorrelations(const std::vector<float> &source, size_t maximumPattern)
      : fft(correlationOrder(source.size(), maximumPattern)), sourceLength(source.size()),
        sums(source.size() + 1), squares(source.size() + 1),
        sourceFrequency(size_t(fft.getSize())), patternTime(size_t(fft.getSize())),
        patternFrequency(size_t(fft.getSize())), product(size_t(fft.getSize())),
        inverse(size_t(fft.getSize())) {
    std::vector<std::complex<float>> time(size_t(fft.getSize()));
    for (size_t i = 0; i < source.size(); ++i) {
      time[i] = {source[i], 0};
      sums[i + 1] = sums[i] + source[i];
      squares[i + 1] = squares[i] + double(source[i]) * source[i];
    }
    fft.perform(time.data(), sourceFrequency.data(), false);
  }

  std::vector<double> scores(const std::vector<float> &pattern,
                             const CancellationCheck &cancelled) {
    if (pattern.empty() || pattern.size() > sourceLength || cancelledNow(cancelled)) return {};
    std::fill(patternTime.begin(), patternTime.end(), std::complex<float>{});
    double sum = 0.0, square = 0.0;
    for (size_t i = 0; i < pattern.size(); ++i) {
      patternTime[i] = {pattern[i], 0}; sum += pattern[i]; square += double(pattern[i]) * pattern[i];
    }
    const double length = double(pattern.size());
    const double energy = square - sum * sum / length;
    if (energy < length * 1e-6) return {};
    fft.perform(patternTime.data(), patternFrequency.data(), false);
    if (cancelledNow(cancelled)) return {};
    for (size_t i = 0; i < product.size(); ++i) product[i] = sourceFrequency[i] * std::conj(patternFrequency[i]);
    fft.perform(product.data(), inverse.data(), true);
    if (cancelledNow(cancelled)) return {};
    std::vector<double> result(sourceLength - pattern.size() + 1);
    for (size_t offset = 0; offset < result.size(); ++offset) {
      if ((offset & 4095) == 0 && cancelledNow(cancelled)) return {};
      const double windowSum = sums[offset + pattern.size()] - sums[offset];
      const double variance = squares[offset + pattern.size()] - squares[offset] - windowSum * windowSum / length;
      if (variance < length * 1e-6 || !std::isfinite(inverse[offset].real())) continue;
      result[offset] = std::clamp((double(inverse[offset].real()) - windowSum * sum / length) /
                                 std::sqrt(variance * energy), -1.0, 1.0);
    }
    return result;
  }

private:
  juce::dsp::FFT fft;
  size_t sourceLength;
  std::vector<double> sums, squares;
  std::vector<std::complex<float>> sourceFrequency, patternTime, patternFrequency, product, inverse;
};

template <typename Sample>
double correlation(const std::vector<Sample> &source, size_t start,
                   const std::vector<Sample> &pattern) {
  if (pattern.empty() || start > source.size() || pattern.size() > source.size() - start) return 0.0;
  double sumSource = 0, sumPattern = 0, sourceEnergy = 0, patternEnergy = 0, dot = 0;
  for (size_t i = 0; i < pattern.size(); ++i) {
    const double s = source[start + i], t = pattern[i];
    sumSource += s; sumPattern += t; sourceEnergy += s * s; patternEnergy += t * t; dot += s * t;
  }
  const double length = double(pattern.size());
  sourceEnergy -= sumSource * sumSource / length;
  patternEnergy -= sumPattern * sumPattern / length;
  // Silence/constant windows must never become numerical high correlations.
  if (sourceEnergy < length * 1e-6 || patternEnergy < length * 1e-6) return 0.0;
  return std::clamp((dot - sumSource * sumPattern / length) /
                    std::sqrt(sourceEnergy * patternEnergy), -1.0, 1.0);
}

struct Candidate {
  const GuideAcousticTemplate *reference = nullptr;
  size_t offset = 0;
  double score = 0.0;
};

std::vector<Candidate> candidatesFor(const std::vector<float> &source,
    SourceCorrelations &correlations, const juce::String &phrase,
    const GuideAcousticTemplateBank &bank, const CancellationCheck &cancelled) {
  std::vector<Candidate> result;
  const auto key = meaning(phrase);
  if (key.isEmpty()) return result;
  for (const auto &reference : bank.templates) {
    if (cancelledNow(cancelled)) return {};
    if (meaning(reference.text) != key || reference.identity.isEmpty() ||
        reference.samples.size() < 1280 || reference.samples.size() > 3 * 16000 ||
        reference.samples.size() > source.size()) continue;
    const auto scores = correlations.scores(reference.samples, cancelled);
    if (scores.empty()) continue;
    std::vector<std::pair<double, size_t>> peaks;
    double bestFft = 0.0, bestFine = 0.0;
    for (size_t offset = 0; offset < scores.size(); ++offset) {
      bestFft = std::max(bestFft, scores[offset]);
      // Account only for FFT float rounding; the direct check below still
      // enforces the original minimumCorrelation without widening it.
      if (scores[offset] >= minimumCorrelation - .0001) peaks.emplace_back(scores[offset], offset);
    }
    std::stable_sort(peaks.begin(), peaks.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<size_t> retained;
    for (const auto &peak : peaks) {
      if (std::any_of(retained.begin(), retained.end(), [&](size_t other) {
            return std::abs(double(other) - double(peak.second)) < .080 * rate; })) continue;
      const auto from = peak.second > 2 ? peak.second - 2 : size_t(0);
      const auto to = std::min(source.size() - reference.samples.size(), peak.second + 2);
      Candidate best{&reference, from, 0};
      for (size_t offset = from; offset <= to; ++offset) {
        const double score = correlation(source, offset, reference.samples);
        if (score > best.score) best = {&reference, offset, score};
      }
      bestFine = std::max(bestFine, best.score);
      if (best.score >= minimumCorrelation) { result.push_back(best); retained.push_back(best.offset); }
      if (retained.size() >= maximumCandidates) break;
    }
    diagnostic(7, double(&reference - bank.templates.data()), bestFft, bestFine,
               double(peaks.size()), double(retained.size()));
  }
  std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
  std::vector<Candidate> unique;
  for (const auto &candidate : result) {
    if (std::any_of(unique.begin(), unique.end(), [&](const auto &other) {
          return std::abs(double(candidate.offset) - double(other.offset)) < .040 * rate; })) continue;
    unique.push_back(candidate);
    if (unique.size() >= maximumCandidates) break;
  }
  return unique;
}

struct Fit { bool valid = false; double residual = 1.0; std::vector<double> conditional; };
Fit fit(const std::vector<float> &source, const std::vector<Candidate> &parts) {
  const auto count = parts.size(), length = source.size();
  std::array<std::array<double, 9>, 8> matrix{};
  std::array<double, 8> sums{}, gains{};
  double sourceSum = 0.0, sourceSquare = 0.0;
  for (auto sample : source) { sourceSum += sample; sourceSquare += double(sample) * sample; }
  const double sourceEnergy = sourceSquare - sourceSum * sourceSum / double(length);
  if (sourceEnergy < double(length) * 1e-6) return {};
  for (size_t i = 0; i < count; ++i) {
    const auto &a = parts[i];
    double rhs = 0.0;
    for (size_t k = 0; k < a.reference->samples.size(); ++k) {
      sums[i] += a.reference->samples[k];
      rhs += double(a.reference->samples[k]) * source[a.offset + k];
    }
    matrix[i][count] = rhs - sums[i] * sourceSum / double(length);
  }
  for (size_t i = 0; i < count; ++i) for (size_t j = 0; j < count; ++j) {
    const auto &a = parts[i], &b = parts[j];
    const auto begin = std::max(a.offset, b.offset);
    const auto end = std::min(a.offset + a.reference->samples.size(), b.offset + b.reference->samples.size());
    double dot = 0.0;
    for (size_t k = begin; k < end; ++k)
      dot += double(a.reference->samples[k - a.offset]) * b.reference->samples[k - b.offset];
    matrix[i][j] = dot - sums[i] * sums[j] / double(length);
  }
  for (size_t i = 0; i < count; ++i) {
    size_t pivot = i;
    for (size_t j = i + 1; j < count; ++j) if (std::abs(matrix[j][i]) > std::abs(matrix[pivot][i])) pivot = j;
    if (std::abs(matrix[pivot][i]) < 1e-10) { diagnostic(51, double(i), matrix[pivot][i]); return {}; }
    if (pivot != i) std::swap(matrix[pivot], matrix[i]);
    const double divisor = matrix[i][i];
    for (size_t j = i; j <= count; ++j) matrix[i][j] /= divisor;
    for (size_t row = 0; row < count; ++row) if (row != i) {
      const double multiplier = matrix[row][i];
      for (size_t j = i; j <= count; ++j) matrix[row][j] -= multiplier * matrix[i][j];
    }
  }
  double dc = sourceSum;
  for (size_t i = 0; i < count; ++i) {
    gains[i] = matrix[i][count];
    if (!std::isfinite(gains[i]) || gains[i] < .02 || gains[i] > 32.0) {
      diagnostic(52, double(i), gains[i]); return {};
    }
    dc -= gains[i] * sums[i];
  }
  dc /= double(length);
  std::vector<float> residual(source.size());
  for (size_t k = 0; k < length; ++k) residual[k] = float(source[k] - dc);
  for (size_t i = 0; i < count; ++i) for (size_t k = 0; k < parts[i].reference->samples.size(); ++k)
    residual[parts[i].offset + k] -= float(gains[i] * parts[i].reference->samples[k]);
  double error = 0.0;
  for (auto sample : residual) error += double(sample) * sample;
  Fit result;
  result.residual = error / sourceEnergy;
  diagnostic(53, result.residual, sourceEnergy, error, double(parts.size()));
  if (!std::isfinite(result.residual) || result.residual > guideAcousticMaximumResidual) return result;
  for (size_t i = 0; i < count; ++i) {
    const auto &candidate = parts[i];
    std::vector<float> isolated(candidate.reference->samples.size());
    for (size_t k = 0; k < isolated.size(); ++k)
      isolated[k] = residual[candidate.offset + k] + float(gains[i] * candidate.reference->samples[k]);
    const double score = correlation(isolated, 0, candidate.reference->samples);
    result.conditional.push_back(score);
    diagnostic(54, double(i), score);
    if (score < guideAcousticMinimumCorrelation) return result;
  }
  result.valid = true;
  return result;
}
} // namespace

juce::File defaultGuideAcousticPackFile() {
  const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  const auto parent = executable.getParentDirectory();
#if JUCE_MAC
  const auto bundled = parent.getSiblingFile("Resources").getChildFile("guides/guides-en-es.zip");
#else
  const auto bundled = parent.getChildFile("installer/guides-en-es.zip");
#endif
  if (bundled.existsAsFile()) return bundled;
  for (auto origin : {juce::File::getCurrentWorkingDirectory(), parent}) {
    for (int depth = 0; depth < 8 && origin.isDirectory(); ++depth) {
      const auto candidate = origin.getChildFile("assets/guides/guides-en-es.zip");
      if (candidate.existsAsFile()) return candidate;
      const auto next = origin.getParentDirectory(); if (next == origin) break; origin = next;
    }
  }
  return {};
}

juce::String guideAcousticPackFingerprint(const juce::File &file, const CancellationCheck &cancelled) {
  if (!file.existsAsFile() || file.getSize() <= 0 || file.getSize() > 64 * 1024 * 1024) return {};
  const auto size = file.getSize(); const auto modified = file.getLastModificationTime();
  auto input = file.createInputStream(); if (!input) return {};
  juce::MemoryOutputStream output;
  std::array<char, 65536> block{};
  while (!input->isExhausted()) {
    if (cancelledNow(cancelled)) return {};
    const int count = input->read(block.data(), int(block.size())); if (count <= 0) return {};
    output.write(block.data(), size_t(count));
  }
  if (file.getSize() != size || file.getLastModificationTime() != modified ||
      juce::int64(output.getDataSize()) != size) return {};
  return juce::SHA256(output.getData(), output.getDataSize()).toHexString();
}

GuideAcousticTemplateBank loadGuideAcousticTemplates(const juce::File &file,
    juce::AudioFormatManager &formats, const CancellationCheck &cancelled, juce::String &error) {
  error.clear(); GuideAcousticTemplateBank result;
  result.fingerprint = guideAcousticPackFingerprint(file, cancelled);
  if (result.fingerprint.isEmpty()) { error = "Known guide recordings unavailable."; return {}; }
  juce::ZipFile archive(file);
  if (archive.getNumEntries() <= 0 || archive.getNumEntries() > 1024) { error = "Invalid known-guide archive."; return {}; }
  std::set<juce::String> seen;
  for (int index = 0; index < archive.getNumEntries(); ++index) {
    if (cancelledNow(cancelled)) { error = "Cancelled."; return {}; }
    const auto *entry = archive.getEntry(index); if (!entry) return {};
    auto path = entry->filename.replaceCharacter('\\', '/');
    const auto parts = juce::StringArray::fromTokens(path, "/", "");
    if (parts.size() != 3 || (parts[0] != "en" && parts[0] != "es") ||
        (parts[1] != "sections" && parts[1] != "cues") ||
        !path.endsWithIgnoreCase(".mp3") || entry->uncompressedSize <= 0 ||
        entry->uncompressedSize > 2 * 1024 * 1024) continue;
    const auto label = filenameLabel(parts[2]); if (label.isEmpty()) continue;
    auto stream = std::unique_ptr<juce::InputStream>(archive.createStreamForEntry(index)); if (!stream) continue;
    juce::MemoryBlock bytes; stream->readIntoMemoryBlock(bytes, size_t(entry->uncompressedSize));
    if (juce::int64(bytes.getSize()) != entry->uncompressedSize) continue;
    const auto identity = parts[0] + ":" + meaning(label) + ":" +
        juce::SHA256(bytes.getData(), bytes.getSize()).toHexString();
    if (!seen.insert(identity).second) continue;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(
        std::make_unique<juce::MemoryInputStream>(bytes, true)));
    if (!reader) continue;
    auto samples = resampleTemplate(*reader, cancelled); if (samples.empty()) continue;
    result.templates.push_back({label, parts[0], identity, std::move(samples)});
    if (result.templates.size() > maximumTemplates) { error = "Too many known guide recordings."; return {}; }
  }
  if (guideAcousticPackFingerprint(file, cancelled) != result.fingerprint) { error = "Known-guide pack changed."; return {}; }
  if (result.templates.empty()) { error = "Known guide recordings could not be decoded."; return {}; }
  return result;
}

std::optional<juce::StringArray> guideAcousticCompoundPhrases(const juce::String &text) {
  return hintPhrases(text);
}

bool guideAcousticAnnouncementsMatchParent(const GuideTranscriptSegment &parent) {
  if (parent.acousticAnnouncements.empty() || parent.acousticAnnouncements.size() > 8 ||
      !parent.acousticResidualRatio || !std::isfinite(*parent.acousticResidualRatio) ||
      *parent.acousticResidualRatio < 0 || *parent.acousticResidualRatio > guideAcousticMaximumResidual ||
      !std::isfinite(parent.startSeconds) || !std::isfinite(parent.endSeconds) ||
      parent.startSeconds < 0 || parent.endSeconds <= parent.startSeconds) return false;
  double previous = -1.0;
  for (const auto &announcement : parent.acousticAnnouncements) {
    if (announcement.text.isEmpty() || announcement.text.length() > 128 ||
        (announcement.language != "en" && announcement.language != "es") ||
        announcement.templateIdentity.isEmpty() || announcement.templateIdentity.length() > 128 ||
        !std::isfinite(announcement.startSeconds) || !std::isfinite(announcement.endSeconds) ||
        announcement.startSeconds < 0 || announcement.endSeconds <= announcement.startSeconds ||
        announcement.startSeconds <= previous ||
        announcement.startSeconds < parent.startSeconds - .200001 ||
        announcement.endSeconds > parent.endSeconds + .300001 ||
        !std::isfinite(announcement.correlation) || announcement.correlation < guideAcousticMinimumCorrelation ||
        announcement.correlation > 1.0) return false;
    previous = announcement.startSeconds;
  }
  if (parent.acousticAnnouncements.size() == 1) {
    const auto actual = meaning(parent.acousticAnnouncements.front().text);
    if (actual.isEmpty() || hintPhrases(parent.text)) return false;
    const auto observed = meaning(parent.text);
    if (observed.isNotEmpty()) return observed == actual;
    return guideAcousticAtomicCandidate(parent);
  }
  return sequenceMatches(parent.text, parent.acousticAnnouncements);
}

bool guideAcousticAtomicCandidate(const GuideTranscriptSegment &parent) {
  if (parent.text.trim().isEmpty() || parent.text.length() > 512 ||
      !std::isfinite(parent.startSeconds) || !std::isfinite(parent.endSeconds) ||
      parent.startSeconds < 0.0 || parent.endSeconds <= parent.startSeconds ||
      parent.endSeconds - parent.startSeconds > 3.0 ||
      (parent.confidence && (!std::isfinite(*parent.confidence) ||
          *parent.confidence < 0.0 || *parent.confidence > 1.0)) ||
      hintPhrases(parent.text) || meaning(parent.text).isNotEmpty()) return false;
  auto original = parent;
  original.acousticAnnouncements.clear(); original.acousticResidualRatio.reset();
  const auto state = interpretGuideAnnouncement(original).recognitionState();
  return state == GuideRecognitionState::unresolved || state == GuideRecognitionState::tentative;
}

GuideAcousticMatch matchKnownGuideAtomic(const std::vector<float> &source,
    double sourceStart, const GuideTranscriptSegment &parent,
    const GuideAcousticTemplateBank &bank, const CancellationCheck &cancelled) {
  GuideAcousticMatch result;
  if (cancelledNow(cancelled)) { result.cancelled = true; return result; }
  if (!guideAcousticAtomicCandidate(parent) || source.size() < 1280 ||
      source.size() > maximumSamples || !std::isfinite(sourceStart) || sourceStart < 0.0 ||
      bank.templates.empty() || bank.templates.size() > maximumTemplates) return result;
  for (auto sample : source) if (!std::isfinite(sample)) return result;
  size_t maximumPattern = 1280;
  std::map<juce::String, juce::String> labels;
  for (const auto &reference : bank.templates) {
    for (auto sample : reference.samples) if (!std::isfinite(sample)) return result;
    const auto key = meaning(reference.text);
    if (key.isNotEmpty()) labels.emplace(key, reference.text);
    maximumPattern = std::max(maximumPattern, reference.samples.size());
  }
  SourceCorrelations correlations(source, maximumPattern);
  std::vector<Candidate> candidates;
  for (const auto &label : labels) {
    auto matches = candidatesFor(source, correlations, label.second, bank, cancelled);
    candidates.insert(candidates.end(), matches.begin(), matches.end());
  }
  if (cancelledNow(cancelled)) { result.cancelled = true; return result; }
  std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
    return a.score > b.score;
  });
  if (candidates.empty() || candidates.front().score < guideAcousticMinimumCorrelation) return result;
  const auto &winner = candidates.front();
  for (const auto &candidate : candidates)
    if (meaning(candidate.reference->text) != meaning(winner.reference->text) &&
        winner.score - candidate.score < .08) {
      result.ambiguous = true; return result;
    }
  const auto complete = fit(source, {winner});
  result.residualRatio = complete.residual;
  if (!complete.valid || complete.conditional.size() != 1) return result;
  const double start = sourceStart + double(winner.offset) / rate;
  GuideAcousticAnnouncement announcement{winner.reference->text, winner.reference->language,
      winner.reference->identity, start, start + double(winner.reference->samples.size()) / rate,
      complete.conditional.front()};
  auto checked = parent;
  checked.acousticAnnouncements = {announcement};
  checked.acousticResidualRatio = complete.residual;
  if (guideAcousticAnnouncementsMatchParent(checked)) result.announcements.push_back(std::move(announcement));
  return result;
}

GuideAcousticMatch matchKnownGuideCompound(const std::vector<float> &source,
    double sourceStart, const GuideTranscriptSegment &parent,
    const GuideAcousticTemplateBank &bank, const CancellationCheck &cancelled) {
  GuideAcousticMatch result;
  if (cancelledNow(cancelled)) { result.cancelled = true; return result; }
  const auto compound = guideAcousticCompoundPhrases(parent.text);
  diagnostic(1, compound ? compound->size() : 0, double(source.size()), parent.confidence.value_or(-1));
  if (!compound || compound->size() < 2 || compound->size() > 8 || source.size() < 1280 || source.size() > maximumSamples ||
      !std::isfinite(sourceStart) || sourceStart < 0 || bank.templates.empty() ||
      bank.templates.size() > maximumTemplates || !parent.confidence ||
      !std::isfinite(*parent.confidence) || *parent.confidence < std::exp(-1.2) || *parent.confidence > 1.0) return result;
  for (auto sample : source) if (!std::isfinite(sample)) return result;
  for (const auto &reference : bank.templates) for (auto sample : reference.samples)
    if (!std::isfinite(sample)) return result;
  size_t maximumPattern = 1280;
  for (const auto &reference : bank.templates) maximumPattern = std::max(maximumPattern, reference.samples.size());
  SourceCorrelations correlations(source, maximumPattern);
  {
    // Search physically recorded labels of observed roles. Count/instruction
    // recordings omitted by ASR can be demonstrated, but new principal section
    // identities are never inferred from this vocabulary search.
    bool countRole = false, utilityRole = false;
    std::map<juce::String, juce::String> labels;
    for (const auto &phrase : *compound) {
      const auto key = meaning(phrase);
      countRole = countRole || key.startsWith("count:");
      utilityRole = utilityRole || key.startsWith("instruction:");
      if (key.startsWith("section:")) labels.emplace(key, phrase);
    }
    for (const auto &reference : bank.templates) {
      const auto key = meaning(reference.text);
      if ((countRole && key.startsWith("count:")) ||
          (utilityRole && key.startsWith("instruction:"))) labels.emplace(key, reference.text);
    }
    std::vector<Candidate> physical;
    for (const auto &label : labels) {
      auto candidates = candidatesFor(source, correlations, label.second, bank, cancelled);
      physical.insert(physical.end(), candidates.begin(), candidates.end());
    }
    diagnostic(3, double(physical.size()), double(labels.size()), countRole, utilityRole);
    if (cancelledNow(cancelled)) { result.cancelled = true; return result; }
    std::stable_sort(physical.begin(), physical.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
    std::vector<Candidate> unique;
    for (const auto &candidate : physical) {
      auto nearby = std::find_if(unique.begin(), unique.end(), [&](const auto &other) {
        return std::abs(double(candidate.offset) - double(other.offset)) < .040 * rate;
      });
      if (nearby != unique.end()) {
        if (meaning(candidate.reference->text) != meaning(nearby->reference->text) &&
            nearby->score - candidate.score < .08) {
          diagnostic(4, nearby->score, candidate.score, double(nearby->offset), double(candidate.offset));
          result.ambiguous = true; return result;
        }
        continue;
      }
      unique.push_back(candidate);
    }
    physical = std::move(unique);
    if (physical.size() > 8) { result.exhausted = true; return result; }
    if (physical.size() < 2) return result;
    std::stable_sort(physical.begin(), physical.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    const auto complete = fit(source, physical);
    result.residualRatio = complete.residual;
    if (!complete.valid) { diagnostic(5, complete.residual, double(physical.size())); return result; }
    for (size_t i = 0; i < physical.size(); ++i) {
      const auto &candidate = physical[i];
      const double start = sourceStart + double(candidate.offset) / rate;
      result.announcements.push_back({candidate.reference->text, candidate.reference->language,
          candidate.reference->identity, start,
          start + double(candidate.reference->samples.size()) / rate, complete.conditional[i]});
    }
    if (!sequenceMatches(parent.text, result.announcements)) {
      diagnostic(6, double(result.announcements.size()), compound->size());
      result.announcements.clear(); return result;
    }
    result.residualRatio = complete.residual;
    return result;
  }
}
namespace {
// Repeated studio cues can have slightly different sub-sample phase after
// rendering. Smooth over nine samples (0.56 ms) without downsampling. A
// separate comparison of the unfiltered recording still has to agree.
std::vector<float> smoothRepetitionAudio(const std::vector<float> &pcm) {
  std::vector<float> result(pcm.size());
  double sum = 0.0;
  constexpr size_t width = 9;
  for (size_t i = 0; i < pcm.size(); ++i) {
    if (!std::isfinite(pcm[i])) return {};
    sum += pcm[i];
    if (i >= width) sum -= pcm[i - width];
    result[i] = float(sum / double(width));
  }
  return result;
}
} // namespace

std::optional<GuideRepetitionMatch> matchGuideRepetition(
    const std::vector<float> &targetPcm16k,
    const std::vector<GuideRepetitionReference> &references,
    const CancellationCheck &cancelled) {
  constexpr size_t minimumLength = 3200, maximumLength = 64000;
  constexpr double margin = .08, minimumEdge = .85;
  if (cancelledNow(cancelled) || references.empty() || references.size() > 64 ||
      targetPcm16k.size() < minimumLength || targetPcm16k.size() > 128000) return {};
  const auto source = smoothRepetitionAudio(targetPcm16k);
  if (source.empty()) return {};
  const auto longest = std::max_element(references.begin(), references.end(), [](const auto &a, const auto &b) {
    return a.samples.size() < b.samples.size();
  });
  if (longest->samples.size() < minimumLength) return {};
  SourceCorrelations search(source, std::min(maximumLength, longest->samples.size()));

  struct Candidate {
    GuideRepetitionMatch match;
    double rank;
    bool accepted;
  };
  std::vector<Candidate> candidates;
  size_t verifiedPeaks = 0;
  for (size_t i = 0; i < references.size(); ++i) {
    if (cancelledNow(cancelled)) return {};
    const auto &reference = references[i];
    const auto length = reference.samples.size();
    if (reference.key.isEmpty() || length < minimumLength || length > maximumLength || length > source.size())
      continue;
    const auto pattern = smoothRepetitionAudio(reference.samples);
    if (pattern.empty()) return {};
    const auto scores = search.scores(pattern, cancelled);
    // A common ending ("la intensidad") can dominate a full-phrase score.
    // Require independently matching speech at *both* ends, at the same lag.
    const size_t edge = std::min<size_t>(3840, length / 3);
    const std::vector<float> prefix(pattern.begin(), pattern.begin() + std::ptrdiff_t(edge));
    const std::vector<float> suffix(pattern.end() - std::ptrdiff_t(edge), pattern.end());
    constexpr size_t block = 320; // check the best source-sample lag per 20 ms
    for (size_t begin = 0; begin < scores.size(); begin += block) {
      if (cancelledNow(cancelled)) return {};
      const auto peak = std::max_element(scores.begin() + std::ptrdiff_t(begin),
          scores.begin() + std::ptrdiff_t(std::min(begin + block, scores.size())));
      if (*peak < guideRepetitionMinimumCorrelation - margin) continue;
      // A highly periodic or duplicated signal can generate many plausible
      // lags. Leave it for review rather than validating unbounded peaks.
      if (++verifiedPeaks > 256) return {};
      const auto offset = size_t(peak - scores.begin());
      if (correlation(targetPcm16k, offset, reference.samples) < .65) continue;
      const double front = correlation(source, offset, prefix);
      const double back = correlation(source, offset + length - edge, suffix);
      const double rank = std::min({*peak, front, back});
      if (rank >= minimumEdge - margin)
        candidates.push_back({{i, int(offset), int(offset + length), *peak}, rank,
            *peak >= guideRepetitionMinimumCorrelation && front >= minimumEdge && back >= minimumEdge});
    }
  }
  if (cancelledNow(cancelled) || candidates.empty()) return {};
  const auto winner = std::max_element(candidates.begin(), candidates.end(),
      [](const Candidate &a, const Candidate &b) { return a.rank < b.rank; });
  if (!winner->accepted) return {};
  for (const auto &other : candidates) {
    if (winner->rank - other.rank > margin) continue;
    if (references[winner->match.referenceIndex].key != references[other.match.referenceIndex].key ||
        std::abs(winner->match.startSample - other.match.startSample) > 640) return {};
  }
  return winner->match;
}
} // namespace playworship::analysis
