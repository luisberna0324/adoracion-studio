#pragma once

#include <JuceHeader.h>

#include <vector>

struct CategoryRoutingDefaults {
  int baseChannel = 1; // 1-based
  int width = 1;       // 1 = mono, 2 = stereo
};

/** Preferencias de la app (Standalone), persistidas en disco. */
class AppSettings {
public:
  /** Por defecto: expandir la ventana al área de trabajo al abrir la app. */
  static constexpr bool kDefaultExpandWindowOnLaunch = true;
  static constexpr bool kDefaultPrimeImportEnabled = false;

  static bool getExpandWindowOnLaunch();
  static void setExpandWindowOnLaunch(bool value);
  static bool getPrimeImportEnabled();
  static void setPrimeImportEnabled(bool value);
  static std::vector<CategoryRoutingDefaults> getRoutingDefaults();
  static void setRoutingDefaults(
      const std::vector<CategoryRoutingDefaults> &defaults);
  static std::vector<CategoryRoutingDefaults>
  getRoutingDefaultsForDevice(const juce::String &deviceKey);
  static void setRoutingDefaultsForDevice(
      const juce::String &deviceKey,
      const std::vector<CategoryRoutingDefaults> &defaults);
  static CategoryRoutingDefaults
  getRoutingDefaultsForCategory(const juce::String &category);

private:
  static juce::File getSettingsFile();
};
