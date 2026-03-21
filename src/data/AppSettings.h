#pragma once

#include <JuceHeader.h>

/** Preferencias de la app (Standalone), persistidas en disco. */
class AppSettings {
public:
  /** Por defecto: expandir la ventana al área de trabajo al abrir la app. */
  static constexpr bool kDefaultExpandWindowOnLaunch = true;

  static bool getExpandWindowOnLaunch();
  static void setExpandWindowOnLaunch(bool value);

private:
  static juce::File getSettingsFile();
};
