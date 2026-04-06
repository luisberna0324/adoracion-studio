@echo off
setlocal enabledelayedexpansion

set JUCE_DIR=C:\Users\luisberna\dev\multitracks\multitracks\third_party\JUCE

cmake --preset windows-release
if errorlevel 1 exit /b 1
cmake --build --preset windows-release
if errorlevel 1 exit /b 1

set APP_DIR=C:\Users\luisberna\dev\multitracks\multitracks\juce-controls-plugin\build\windows-release\PlayWorship_artefacts\Standalone
if exist "%APP_DIR%" (
  for %%F in ("%APP_DIR%\*.exe") do (
    start "" "%%F"
    goto :eof
  )
  echo No .exe found in %APP_DIR%
) else (
  echo App directory not found: %APP_DIR%
)

endlocal
