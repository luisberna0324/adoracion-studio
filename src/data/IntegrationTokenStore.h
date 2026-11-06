#pragma once
#include <JuceHeader.h>
namespace IntegrationTokenStore {
bool available();
juce::String account(const juce::String& provider, const juce::String& host, int port);
juce::String load(const juce::String& provider, const juce::String& host, int port);
bool save(const juce::String& provider, const juce::String& host, int port, const juce::String& token);
bool forget(const juce::String& provider, const juce::String& host, int port);
}
