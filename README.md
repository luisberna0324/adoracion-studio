# MT Controls (JUCE)

Proyecto base de plugin JUCE para iterar diseño de controles.

## Incluye

- Plugin `VST3` + `Standalone`
- Parámetros iniciales:
  - `inputGain` (`-24 dB` a `+24 dB`)
  - `mix` (`0%` a `100%`)
  - `bypass` (`on/off`)
- UI inicial con dos knobs y un toggle conectados por `AudioProcessorValueTreeState`

## Requisitos

- CMake 3.22+
- JUCE (clonado localmente)
- Toolchain C++ (Xcode en macOS o Visual Studio en Windows)

## Build

```bash
cd juce-controls-plugin
cmake -S . -B build -DJUCE_DIR=/Users/luisberna/dev/multitracks/multitracks/third_party/JUCE
cmake --build build -j
```

## Siguiente foco

Iterar el diseño de controles (`LookAndFeel`, layout por secciones, estados hover/drag, tipografía y palette).
