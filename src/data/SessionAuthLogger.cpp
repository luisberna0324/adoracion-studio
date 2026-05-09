#include "SessionAuthLogger.h"

#include <cstdio>
#include <memory>

namespace {

juce::CriticalSection gMutex;
std::unique_ptr<juce::FileLogger> gLogger;
/** Ruta del fichero que usa gLogger; vacío si no hay logger. */
juce::File gResolvedLogFile;
bool gTriedInit = false;

juce::File appSupportLogFile() {
  return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
      .getChildFile("MTControls")
      .getChildFile("session_auth.log");
}

juce::File documentsLogFile() {
  return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
      .getChildFile("MTControls")
      .getChildFile("session_auth.log");
}

juce::File systemLogsLogFile() {
  return juce::FileLogger::getSystemLogFileFolder()
      .getChildFile("MTControls")
      .getChildFile("session_auth.log");
}

bool tryInstallLogger(const juce::File &logFile) {
  const auto parent = logFile.getParentDirectory();
  const auto dirRes = parent.createDirectory();
  if (!dirRes.wasOk() && !parent.isDirectory()) {
    juce::Logger::writeToLog(
        "[SessionAuthLogger] createDirectory fallo path=" +
        parent.getFullPathName() + " err=" + dirRes.getErrorMessage());
    return false;
  }

  // Misma estrategia que juce::FileLogger: create() del fichero y
  // FileOutputStream en logMessage (mas fiable que appendText en algunos hosts).
  gLogger = std::make_unique<juce::FileLogger>(
      logFile, "MTControls session auth", static_cast<juce::int64>(-1));
  gResolvedLogFile = logFile;
  return true;
}

void ensureLoggerLocked() {
  if (gTriedInit)
    return;
  gTriedInit = true;

  if (tryInstallLogger(appSupportLogFile()))
    return;
  if (tryInstallLogger(documentsLogFile()))
    return;
  if (tryInstallLogger(systemLogsLogFile()))
    return;

  gLogger.reset();
  gResolvedLogFile = juce::File();
  juce::Logger::writeToLog(
      "[SessionAuthLogger] No se pudo crear session_auth.log (probado "
      "Application Support/MTControls, Documents/MTControls y "
      "Library/Logs/MTControls).");
}

} // namespace

namespace SessionAuthLogger {

juce::File getLogFile() {
  const juce::ScopedLock lock(gMutex);
  ensureLoggerLocked();
  if (gResolvedLogFile.getFullPathName().isNotEmpty())
    return gResolvedLogFile;
  return appSupportLogFile();
}

juce::String tokenPresence(const juce::String &maybeToken) {
  const auto n = static_cast<juce::int64>(maybeToken.getNumBytesAsUTF8());
  return n > 0 ? ("len=" + juce::String(n)) : juce::String("empty");
}

void write(const juce::String &message) {
  const juce::ScopedLock lock(gMutex);
  ensureLoggerLocked();

  const auto prefixed = juce::String("[SessionAuth] ") + message;

  const auto line = prefixed + "\n";
  std::fputs(line.toRawUTF8(), stderr);
  std::fflush(stderr);

  // Consola: stderr / depurador (macOS: fputs a stderr). En Debug, FileLogger ya
  // hace DBG() en logMessage; evitamos duplicar la misma linea.
#if JUCE_DEBUG
  if (gLogger == nullptr)
    DBG(prefixed);
#else
  juce::Logger::outputDebugString(prefixed);
#endif

  if (gLogger != nullptr)
    gLogger->logMessage(prefixed);
  else
    juce::Logger::writeToLog(prefixed);
}

} // namespace SessionAuthLogger
