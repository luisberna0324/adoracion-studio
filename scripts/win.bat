@echo off
setlocal enabledelayedexpansion

set JUCE_DIR=C:\Users\luisberna\dev\multitracks\multitracks\third_party\JUCE

set TARGET_ARCH=%1
if "%TARGET_ARCH%"=="" set TARGET_ARCH=x64

set PRESET=windows-release
set BUILD_DIR=windows-release
if /I "%TARGET_ARCH%"=="x86" (
  set PRESET=windows-x86-release
  set BUILD_DIR=windows-x86-release
)

cmake --preset %PRESET%
if errorlevel 1 exit /b 1
cmake --build --preset %PRESET%
if errorlevel 1 exit /b 1

set APP_DIR=C:\Users\luisberna\dev\multitracks\multitracks\juce-controls-plugin\build\%BUILD_DIR%\MTControls_artefacts\Standalone
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
