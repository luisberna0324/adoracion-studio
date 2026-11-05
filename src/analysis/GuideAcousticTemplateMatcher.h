#pragma once

#include "GuideTranscription.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

namespace playworship::analysis {

inline constexpr double guideAcousticMinimumCorrelation = .94;
inline constexpr double guideAcousticMaximumResidual = .12;

// These are recordings distributed with the app, not expected test labels.
// Samples are mono 16 kHz and trimmed to independently measured voice activity.
struct GuideAcousticTemplate {
  juce::String text, language, identity;
  std::vector<float> samples;
};

struct GuideAcousticTemplateBank {
  juce::String fingerprint;
  std::vector<GuideAcousticTemplate> templates;
};

struct GuideAcousticMatch {
  std::vector<GuideAcousticAnnouncement> announcements;
  double residualRatio = 1.0;
  bool cancelled = false, ambiguous = false, exhausted = false;
};

// All operations are worker-only and read-only. A missing/invalid pack is an
// unavailable fallback; it never downloads/unpacks files or changes playback.
juce::File defaultGuideAcousticPackFile();
juce::String guideAcousticPackFingerprint(const juce::File &,
                                        const CancellationCheck &cancelled = {});
GuideAcousticTemplateBank loadGuideAcousticTemplates(
    const juce::File &, juce::AudioFormatManager &, const CancellationCheck &,
    juce::String &error);

// Eligibility is established from complete literal ASR parts. The special
// unnumbered-prefix + single count case is only a candidate: the audio must
// independently demonstrate both recordings before its ordinal is changed.
std::optional<juce::StringArray> guideAcousticCompoundPhrases(const juce::String &text);
bool guideAcousticAnnouncementsMatchParent(const GuideTranscriptSegment &);

// No BPM, beat positions, sequence manifest or expected timestamps enter this
// API. Source start and all returned positions are absolute source seconds.
GuideAcousticMatch matchKnownGuideCompound(
    const std::vector<float> &sourcePcm16k, double sourceStartSeconds,
    const GuideTranscriptSegment &parent, const GuideAcousticTemplateBank &,
    const CancellationCheck &cancelled = {});

} // namespace playworship::analysis
