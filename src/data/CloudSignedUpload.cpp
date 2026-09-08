#include "CloudSignedUpload.h"

CloudSignedUploadResult uploadFileToSignedUrl(const juce::String& urlText,
                                              const juce::File& file,
                                              const juce::String& contentType,
                                              const int timeoutMs) {
  CloudSignedUploadResult result;
  if (!file.existsAsFile()) {
    result.errorMessage = "Local upload file is unavailable.";
    return result;
  }
  juce::MemoryBlock body;
  if (!file.loadFileAsData(body)) {
    result.errorMessage = "Couldn't read local upload file.";
    return result;
  }
  int status = 0;
  auto request = juce::URL(urlText).withPOSTData(body);
  auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
      .withExtraHeaders("Content-Type: " + contentType + "\r\n")
      .withConnectionTimeoutMs(timeoutMs).withHttpRequestCmd("PUT").withStatusCode(&status);
  auto response = request.createInputStream(options);
  result.httpCode = status;
  if (!response) { result.errorMessage = "Signed upload unavailable."; return result; }
  result.responseBody = response->readEntireStreamAsString();
  result.ok = status >= 200 && status < 300;
  if (!result.ok) result.errorMessage = "Signed upload failed.";
  return result;
}
