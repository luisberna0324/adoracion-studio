#pragma once

#include <juce_core/juce_core.h>

namespace playworship::learning {

// Observes a tutorial exercise; it never sends a command or changes playback.
class RemoteLearningAction {
public:
  void begin(bool shouldBePlaying) {
    active = true;
    expectedPlaying = shouldBePlaying;
    resetEvidence();
  }
  void deactivate() { active = false; resetEvidence(); }
  void disconnected() { resetEvidence(); }

  void sent(const juce::String &id, const juce::String &action,
            const juce::String &song, double nowMs, bool playingBefore = false) {
    if (!active || id.isEmpty() || song.isEmpty() ||
        action != (expectedPlaying ? "transport.toggle" : "transport.stop")) return;
    resetEvidence();
    // A toggle from playing is a Pause exercise, even if another client
    // subsequently resumes the shared transport before its snapshot arrives.
    if (expectedPlaying && playingBefore) return;
    pendingId = id;
    songId = song;
    expiresAt = nowMs + 5000.0;
  }
  void acknowledge(const juce::String &id, bool accepted, juce::int64 sequence,
                   const juce::String &reason) {
    if (pendingId.isEmpty() || id != pendingId) return;
    if (!accepted) {
      resetEvidence();
      rejection = reason;
      return;
    }
    acknowledgedAt = sequence;
  }
  void snapshot(juce::int64 sequence, const juce::String &song, bool playing,
                double nowMs) {
    if (!songId.isEmpty() && song != songId) { resetEvidence(); return; }
    if (pendingId.isEmpty()) return;
    if (song != songId || nowMs > expiresAt) { resetEvidence(); return; }
    if (acknowledgedAt >= 0 && sequence > acknowledgedAt && playing == expectedPlaying) {
      confirmed = true;
      pendingId.clear();
    }
  }
  bool applied() const { return confirmed; }
  const juce::String &reason() const { return rejection; }

private:
  void resetEvidence() {
    pendingId.clear(); songId.clear(); rejection.clear();
    acknowledgedAt = -1; expiresAt = 0; confirmed = false;
  }
  bool active = false, expectedPlaying = false, confirmed = false;
  juce::String pendingId, songId, rejection;
  juce::int64 acknowledgedAt = -1;
  double expiresAt = 0;
};

} // namespace playworship::learning
