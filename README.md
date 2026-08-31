# MT Controls (JUCE)

Proyecto base JUCE para iterar diseno de controles (Standalone).

## Incluye

- Standalone (macOS, Windows, iOS)
- Parametros iniciales:
  - `inputGain` (`-24 dB` a `+24 dB`)
  - `mix` (`0%` a `100%`)
  - `bypass` (`on/off`)
- UI inicial con dos knobs y un toggle conectados por `AudioProcessorValueTreeState`

## Requisitos

- CMake 3.22+
- JUCE (clonado localmente)
- Toolchain C++ (Xcode en macOS o Visual Studio en Windows)

## Build (CMake Presets)

Configura la variable de entorno `JUCE_DIR` apuntando al repo local de JUCE.

### Nota sobre JUCE_DIR en Windows

PowerShell:
```powershell
$env:JUCE_DIR="C:\ruta\JUCE"
```

CMD:
```cmd
set JUCE_DIR=C:\ruta\JUCE
```

### macOS

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

### Windows

```bash
cmake --preset windows-release
cmake --build --preset windows-release
```

Windows 32-bit (x86):

```bash
cmake --preset windows-x86-release
cmake --build --preset windows-x86-release
```

## Instaladores (MVP)

### macOS (PKG installer)

Genera el build y luego empaqueta el instalador `.pkg`:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
bash scripts/package_macos_pkg.sh --version 1.2.3
```

Salida esperada:

`dist/playworship-macos-installer-1.2.3.pkg`

### Windows (Inno Setup EXE)

Genera el build y luego empaqueta el instalador `.exe`:

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
.\scripts\package_windows_installer.ps1 -Version 1.2.3 -Architecture x64
.\scripts\package_windows_installer.ps1 -Version 1.2.3 -Architecture x86
```

Salida esperada:

- `dist/playworship-windows-installer-1.2.3-x64.exe`
- `dist/playworship-windows-installer-1.2.3-x86.exe`

### iOS

```bash
cmake --preset ios-release
cmake --build --preset ios-release
```

El proyecto de Xcode se genera en `build/ios-ipad-flatbundle` para firmar y ejecutar
en dispositivo.

Abrir en Xcode:
```bash
open build/ios-ipad-flatbundle/MTControls.xcodeproj
```

## Build (manual)

Si prefieres configurar sin presets:

```bash
cmake -S . -B build -DJUCE_DIR=/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE
cmake --build build -j
```

## CI (idea)

- Windows: GitHub Actions con `windows-latest` + presets `windows-release` (x64) y `windows-x86-release` (x86)
- macOS: GitHub Actions con `macos-latest` + preset `macos-release`
- iOS: generar proyecto Xcode con preset `ios-release` (genera `build/ios-ipad-flatbundle`, requiere signing para build/run)

### Workflow `Build Artifacts`

Entradas relevantes:

- `publish_release`: publica (o actualiza) GitHub Release.
- `publish_installers` (default `true`): genera/publica instaladores `pkg` y `exe`.

Assets publicados cuando `publish_release=true`:

- `MTControls-macos-standalone-<semver>.zip`
- `MTControls-windows-x64-standalone-<semver>.zip`
- `MTControls-windows-x86-standalone-<semver>.zip`
- `playworship-macos-installer-<semver>.pkg` (si `publish_installers=true`)
- `playworship-windows-installer-<semver>-x64.exe` (si `publish_installers=true`)
- `playworship-windows-installer-<semver>-x86.exe` (si `publish_installers=true`)

## Siguiente foco

Iterar el diseno de controles (`LookAndFeel`, layout por secciones, estados hover/drag, tipografia y palette).

Se agrega nueva linea para probar el feature
