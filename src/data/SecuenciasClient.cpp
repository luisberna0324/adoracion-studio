#include "SecuenciasClient.h"

// Retired for every distribution: no third-party catalog requests or caches.
SecuenciasClient::SecuenciasClient() : pool(2) {}
SecuenciasClient::~SecuenciasClient() = default;
void SecuenciasClient::cancelAll() {}
void SecuenciasClient::searchSongs(const juce::String&, int, SearchCallback cb) {
  if (cb) cb({}, {SecuenciasError::Type::InvalidInput, 0,
                  "External catalog unavailable", {}});
}
void SecuenciasClient::getSongDetails(const juce::String&, int, DetailCallback cb) {
  if (cb) cb({}, {SecuenciasError::Type::InvalidInput, 0,
                  "External catalog unavailable", {}});
}
