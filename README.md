# MT Controls (JUCE)

Proyecto base JUCE para iterar diseño de controles (Standalone).

## Incluye

- Standalone (macOS, Windows, iOS)
- Parámetros iniciales:
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

### iOS

```bash
cmake --preset ios-release
cmake --build --preset ios-release
```

El proyecto de Xcode se genera en `build/ios-release` para firmar y ejecutar
en dispositivo.

Abrir en Xcode:
```bash
open build/ios-release/MTControls.xcodeproj
```

## Build (manual)

Si prefieres configurar sin presets:

```bash
cmake -S . -B build -DJUCE_DIR=/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE
cmake --build build -j
```

## CI (idea)

- Windows: GitHub Actions con `windows-latest` + preset `windows-release`
- macOS: GitHub Actions con `macos-latest` + preset `macos-release`
- iOS: generar proyecto Xcode con preset `ios-release` (requiere signing para build/run)

## Siguiente foco

Iterar el diseño de controles (`LookAndFeel`, layout por secciones, estados hover/drag, tipografía y palette).
