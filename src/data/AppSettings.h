#pragma once

#include <JuceHeader.h>

#include <vector>

struct CategoryRoutingDefaults {
  int baseChannel = 1; // 1-based
  int width = 1;       // 1 = mono, 2 = stereo
};

enum class AppLanguage { Spanish = 0, English = 1 };

/** Preferencias de la app (Standalone), persistidas en disco. */
class AppSettings {
public:
  /** Por defecto: expandir la ventana al área de trabajo al abrir la app. */
  static constexpr bool kDefaultExpandWindowOnLaunch = true;
  static constexpr bool kDefaultPrimeImportEnabled = false;
  static constexpr AppLanguage kDefaultLanguage = AppLanguage::Spanish;

  static bool getExpandWindowOnLaunch();
  static void setExpandWindowOnLaunch(bool value);
  static bool getPrimeImportEnabled();
  static void setPrimeImportEnabled(bool value);
  static AppLanguage getLanguage();
  static void setLanguage(AppLanguage value);
  static int getThemePresetId();
  static void setThemePresetId(int presetId);
  static juce::String getLastSelectedSongId();
  static void setLastSelectedSongId(const juce::String &songId);
  static juce::int64 getLastSuccessfulLoginMs();
  static void setLastSuccessfulLoginMs(juce::int64 value);
  static juce::int64 getReauthGraceDeadlineMs();
  static void setReauthGraceDeadlineMs(juce::int64 value);
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
