---
type: MIDI System
title: MidiBindings
description: Sistema de binding MIDI con MIDI learn
resource: src/PluginProcessor.h
tags: [juce, midi, binding, learn]
timestamp: 2026-07-14T20:45:00Z
---

# MIDI Bindings

Sistema de mapeo de mensajes MIDI a targets del plugin.

## Conceptos

### Source (Fuente)

El mensaje MIDI que se recibe:
- **CC (Control Change)**: Perillas, faders, botones
- **Note**: Teclado, pads
- **Pitch Bend**: Wheel de pitch
- **Aftertouch**: Presión post-tecla

### Target (Destino)

El parámetro del plugin que se controla (ver [MidiTargets](MidiTargets.md))

### Binding Mode

Cómo se interpreta el valor MIDI:

| Modo | Descripción |
|------|-------------|
| `Absolute` | Valor directo (0-127) |
| `Relative` | Cambio incremental (+/-) |
| `Toggle` | On/Off alternado |

## MidiBinding Struct

```cpp
struct MidiBinding {
    MidiBindingSourceType sourceType;
    int channel;          // 0-15
    int data1;            // CC# o note#
    int targetId;
    MidiBindingMode mode;
    float min;            // Mapeo mínimo
    float max;            // Mapeo máximo
};
```

## MidiInputProbe

Último mensaje MIDI recibido (para debugging/display):

```cpp
struct MidiInputProbe {
    juce::int64 timestampMs = 0;
    int channel = 0;
    int data1 = -1;
    int data2 = -1;
    int type = 0;  // 1=CC, 2=NoteOn, 3=NoteOff, 4=Other
};
```

## API Pública

### Consulta de bindings

```cpp
std::vector<MidiBinding> getMidiBindings() const;
```

Retorna todos los bindings configurados.

### Configuración de bindings

```cpp
void setMidiBinding(const MidiBinding &binding);
```

Asigna un binding específico.

```cpp
void clearMidiBindingForTarget(int targetId);
```

Elimina el binding de un target.

### MIDI Learn

```cpp
void beginMidiLearnForTarget(int targetId);
```

Inicia modo MIDI Learn para el target especificado.
Cuando el usuario mueve un control MIDI, se captura el mensaje.

```cpp
bool consumePendingMidiLearnBinding(MidiBinding &outBinding);
```

Consume el binding pendiente del MIDI Learn.

### Probing

```cpp
MidiInputProbe getLastMidiInputProbe() const noexcept;
```

Retorna el último mensaje MIDI recibido.

### UI Commands

```cpp
bool consumePendingMidiUiCommand(int &outCommand);
```

Consume comandos de UI pendientes por MIDI.

**Comandos UI disponibles:**

| ID | Constante | Acción |
|----|-----------|--------|
| 1 | `midiUiCommandPrevSong` | Canción anterior |
| 2 | `midiUiCommandNextSong` | Siguiente canción |
| 3 | `midiUiCommandToggleWindowMode` | Toggle fullscreen |
| 4 | `midiUiCommandToggleSettings` | Toggle settings |
| 5 | `midiUiCommandShowSongWindowPopover` | Mostrar popover canción |
| 6 | `midiUiCommandShowKeyPopover` | Mostrar popover tonalidad |
| 7 | `midiUiCommandShowMetronomePopover` | Mostrar popover metrónomo |
| 8 | `midiUiCommandMixerNudgeUp` | Subir fader |
| 9 | `midiUiCommandMixerNudgeDown` | Bajar fader |
| 10 | `midiUiCommandMixerApplyRouting` | Aplicar routing |
| 11 | `midiUiCommandWaveformEditToggle` | Toggle edit |

## Implementación

### Snapshots lock-free

El sistema usa triple-buffering para actualizar bindings sin bloquear:

```cpp
struct MidiBindingSnapshot {
    int count = 0;
    std::array<MidiBinding, maxRealtimeMidiBindings> bindings{};
};

std::array<MidiBindingSnapshot, 3> midiBindingSnapshots{};
std::atomic<int> publishedMidiBindingSnapshot{0};
```

- **Writer**: UI thread escribe nuevos bindings
- **Reader**: Audio thread lee snapshot actual
- **Atomic swap**: Cambio de índice atómico

### Lookup rápido

```cpp
int midiSourceStateIndex(MidiBindingSourceType sourceType, int channel, int data1) noexcept;
```

Índice para acceso O(1) a último valor de source:
```
index = sourceType * 16 * 128 + channel * 128 + data1
```

## Ejemplo de uso

### Configurar binding manual

```cpp
// Bind CC7 (Volume) a stem 0 volume
MidiBinding binding;
binding.sourceType = MidiBindingSourceType::CC;
binding.channel = 0;
binding.data1 = 7;
binding.targetId = MTControlsAudioProcessor::midiTargetStemVolumeBase + 0;
binding.mode = MidiBindingMode::Absolute;
binding.min = 0.0f;
binding.max = 1.0f;

processor.setMidiBinding(binding);
```

### MIDI Learn workflow

```cpp
// 1. Iniciar MIDI Learn para volumen master
processor.beginMidiLearnForTarget(midiTargetMasterVolume);

// 2. Usuario mueve un control en su hardware
// El sistema captura el mensaje

// 3. En UI, verificar si hay binding pendiente
MidiBinding pendingBinding;
if (processor.consumePendingMidiLearnBinding(pendingBinding)) {
    // Mostrar confirmación al usuario
    showBindingConfirmation(pendingBinding);
    // O guardar automáticamente
    processor.setMidiBinding(pendingBinding);
}
```

### Probing para display

```cpp
void updateMidiDisplay() {
    auto probe = processor.getLastMidiInputProbe();
    
    if (probe.type == 1) {  // CC
        std::cout << "CC" << probe.channel << ":" 
                  << probe.data1 << " = " << probe.data2 << "\n";
    }
}
```

### Manejo de UI commands

```cpp
void onMidiMessage(const MidiMessage &msg) {
    int command;
    if (processor.consumePendingMidiUiCommand(command)) {
        switch (command) {
            case midiUiCommandPrevSong:
                loadPreviousSong();
                break;
            case midiUiCommandNextSong:
                loadNextSong();
                break;
            case midiUiCommandToggleWindowMode:
                toggleWindowMode();
                break;
            // ...
        }
    }
}
```

## Persistencia

Los bindings se guardan con el estado del plugin:

```cpp
// Guardar
MemoryBlock state;
processor.getStateInformation(state);
saveToFile(state);

// Cargar
MemoryBlock state = loadFromFile();
processor.setStateInformation(state.getData(), state.getSize());
```

## Límites

```cpp
static constexpr int maxRealtimeMidiBindings = 512;
static constexpr int midiSourceStateSlots = 2 * 16 * 128;  // CC + Note × 16ch × 128nums
```

## Archivos relacionados

- [MidiTargets](MidiTargets.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
