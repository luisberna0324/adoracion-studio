#pragma once

#include <JuceHeader.h>

struct CloudSignedUploadResult {
  bool ok = false;
  int httpCode = 0;
  juce::String responseBody;
  juce::String errorMessage;
};

/** Streams a local file directly to a signed object-store PUT URL. */
CloudSignedUploadResult uploadFileToSignedUrl(const juce::String& url,
                                              const juce::File& file,
                                              const juce::String& contentType,
                                              int timeoutMs);
