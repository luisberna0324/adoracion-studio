#pragma once

#include <JuceHeader.h>

#include <vector>

struct CategoryRoutingDefaults {
  int baseChannel = 1; // 1-based
  int width = 1;       // 1 = mono, 2 = stereo
};

struct PadsPackSettings {
  juce::String folderPath;
  juce::String sourcePath;
  juce::String hash;
  juce::int64 importedAtMs = 0;
  bool valid = false;
  juce::String status;
};

struct AssetsBootstrapState {
  juce::String encoderVersion;
  juce::String encoderStatus;
  juce::String padsVersion;
  juce::String padsStatus;
  juce::String lastError;
  juce::int64 updatedAtMs = 0;
};

enum class MidiBindingMode { Absolute = 0, Toggle = 1, Momentary = 2 };
enum class MidiBindingSourceType { CC = 0, Note = 1 };

struct MidiBindingSetting {
  int targetId = -1;
  int midiChannel = 1; // 1..16
  int ccNumber = 0;    // 0..127 (CC number o Note number segun sourceType)
  MidiBindingSourceType sourceType = MidiBindingSourceType::CC;
  MidiBindingMode mode = MidiBindingMode::Absolute;
};

enum class AppLanguage { Spanish = 0, English = 1 };
enum class AppLayoutMode { Auto = 0, Compact = 1, Standard = 2, Large = 3 };
enum class PadsCrossfadePreset { Fast = 0, Medium = 1, Long = 2 };
enum class MetronomeClickStyle {
  Classic = 0,
  Woodblock = 1,
  Cowbell = 2,
  Digital = 3,
  Gentle = 4,
  Percussive = 5,
  Saw = 6,
  Blip = 7
};

/** Preferencias de la app (Standalone), persistidas en disco. */
class AppSettings {
public:
  /** Por defecto: expandir la ventana al area de trabajo al abrir la app. */
  static constexpr bool kDefaultExpandWindowOnLaunch = true;
  static constexpr bool kDefaultPrimeImportEnabled = false;
  static constexpr bool kDefaultStemOptimizationEnabled = false;
  static constexpr AppLanguage kDefaultLanguage = AppLanguage::Spanish;
  static constexpr PadsCrossfadePreset kDefaultPadsCrossfadePreset =
      PadsCrossfadePreset::Medium;
  static constexpr MetronomeClickStyle kDefaultMetronomeClickStyle =
      MetronomeClickStyle::Classic;

  static bool getExpandWindowOnLaunch();
  static bool getDemoWelcomeSeen();
  static bool hasServiceTermsIntroSeen(const juce::String& version);
  static bool acknowledgeServiceTermsIntro(const juce::String& version, juce::int64 viewedAtMs);
  static juce::int64 serviceTermsAcceptedAtMs(const juce::String& version);
  static bool hasServiceTermsAcceptance(const juce::String& version);
  static bool acceptServiceTermsLocally(const juce::String& version, juce::int64 acceptedAtMs);
  static void setDemoWelcomeSeen(bool value);
  // Local demo preference, separate from the authenticated account snapshot.
  static bool getLocalDemoSessionActive();
  static void setLocalDemoSessionActive(bool value);
  static void setExpandWindowOnLaunch(bool value);
  static bool getPrimeImportEnabled();
  static void setPrimeImportEnabled(bool value);
  static bool getStemOptimizationEnabled();
  static void setStemOptimizationEnabled(bool value);
  static PadsCrossfadePreset getPadsCrossfadePreset();
  static void setPadsCrossfadePreset(PadsCrossfadePreset preset);
  static AppLanguage getLanguage();
  static void setLanguage(AppLanguage value);
  static MetronomeClickStyle getMetronomeClickStyle();
  static void setMetronomeClickStyle(MetronomeClickStyle value);
  static AppLayoutMode getLayoutMode();
  static void setLayoutMode(AppLayoutMode mode);
  static int getThemePresetId();
  static void setThemePresetId(int presetId);
  static juce::String getLastSelectedSongId();
  static void setLastSelectedSongId(const juce::String &songId);
  static juce::String getRemoteServerAlias();
  static void setRemoteServerAlias(const juce::String &alias);
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
  static PadsPackSettings getPadsPackSettings();
  static void setPadsPackSettings(const PadsPackSettings &settings);
  static AssetsBootstrapState getAssetsBootstrapState();
  static void setAssetsBootstrapState(const AssetsBootstrapState &state);
  static std::vector<MidiBindingSetting> getMidiBindings();
  static void setMidiBindings(const std::vector<MidiBindingSetting> &bindings);

private:
  static juce::File getSettingsFile();
};
