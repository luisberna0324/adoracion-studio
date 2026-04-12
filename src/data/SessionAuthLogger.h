#pragma once

#include <JuceHeader.h>

/**
 * Registro de diagnostico para reanudacion de sesion: fichero (varias rutas de
 * fallback) y consola (stderr / Logger::outputDebugString en Release; DBG si no
 * hay fichero en Debug).
 */
namespace SessionAuthLogger {
/**
 * Ruta efectiva del log tras la primera escritura (o getLogFile), en orden:
 * macOS: ~/Library/MTControls (userApplicationDataDirectory de JUCE; no es
 * Application Support), luego ~/Documents/MTControls, luego ~/Library/Logs/MTControls.
 */
juce::File getLogFile();

void write(const juce::String &message);
/** Indica si hay refresh token / id token (solo longitudes, nunca el valor). */
juce::String tokenPresence(const juce::String &maybeToken);
} // namespace SessionAuthLogger
