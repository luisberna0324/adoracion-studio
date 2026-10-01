#pragma once

#include <algorithm>
#include <cmath>

// Presentation preference only. Zoom changes the number of source seconds
// visible around the playhead; it never seeks or changes playback state.
class PracticeViewport {
public:
  explicit PracticeViewport(const double songLengthSeconds = 0.0) {
    setLength(songLengthSeconds);
  }

  double getSpan() const { return std::min(lengthSeconds, requestedSpanSeconds); }

  void setLength(const double seconds) {
    lengthSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
  }

  void reset() { requestedSpanSeconds = defaultSpanSeconds; }

  bool setSpan(const double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0)
      return false;
    const double previous = getSpan();
    requestedSpanSeconds = std::clamp(seconds, minimumSpanSeconds, maximumSpanSeconds);
    return std::abs(getSpan() - previous) > 0.000001;
  }

  // A scale factor of one leaves zoom unchanged; spreading the fingers (>1)
  // enlarges the waveform by reducing the visible duration.
  bool zoomBy(const double factor) {
    return std::isfinite(factor) && factor > 0.0 && factor != 1.0
        ? setSpan(getSpan() / factor) : false;
  }

  static constexpr double minimumSpanSeconds = 2.0;
  static constexpr double maximumSpanSeconds = 48.0;
  static constexpr double defaultSpanSeconds = 12.0;

private:
  double lengthSeconds = 0.0;
  double requestedSpanSeconds = defaultSpanSeconds;
};
