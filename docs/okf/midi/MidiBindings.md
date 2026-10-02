---
type: MIDI System
title: MidiBindings
description: Sistema de binding MIDI con MIDI learn
resource: src/PluginProcessor.h
tags: [juce, midi, binding, learn]
timestamp: 2026-10-02
---

# MIDI Bindings

Sistema de mapeo de mensajes MIDI a targets del plugin.

## Automatización de secciones

Un borrador iniciado localmente captura los cambios MIDI de volumen, ganancia y
mute instrumentales, además de los controles de Pads, usando los mismos setters
que la interfaz. La mezcla base permanece separada.

Si el borrador pertenece a un cliente remoto, el servidor bloquea esos targets
MIDI físicos y descarta sus notas/órdenes de Pads pendientes. El propietario se
publica al iniciar el borrador y se libera al terminar, cancelar, desconectar o
cargar otra canción. Master, Solo, Click/Guía y controles excluidos mantienen sus
bindings manuales. Véase [SectionAutomation](../engine/SectionAutomation.md).

## Conceptos

### Source (Fuente)

Los bindings aceptan **CC (Control Change)** y **Note On/Off**. Otros mensajes
pueden aparecer en el probe de diagnóstico, pero no activan un binding.

### Target (Destino)

El parámetro del plugin que se controla (ver [MidiTargets](MidiTargets.md))

### Binding Mode

Cómo se interpreta el valor MIDI:

| Modo | Descripción |
|------|-------------|
| `Absolute` | Valor directo (0-127) |
| `Toggle` | Activa sólo al pulsar; un CC se dispara al cruzar de 0-63 a 64-127 |
| `Momentary` | Activa al pulsar y desactiva al soltar |

Los targets continuos, como volumen y gain, siguen el valor recibido aun si
el binding estaba configurado como `Toggle` o `Momentary`. En notas, `Toggle`
ignora Note Off; `Momentary` entrega 127 en Note On y 0 en Note Off. En CC,
`Momentary` entrega también el valor de la bajada bajo 64. La decisión de
disparo se resuelve en `src/audio/MidiBindingTrigger.h`, usado por el callback
de audio sin asignaciones de memoria.

## MidiBinding Struct

```cpp
struct MidiBindingSetting {
    int targetId;
    int midiChannel;      // 1..16
    int ccNumber;         // 0..127: número de CC o nota
    MidiBindingSourceType sourceType; // CC o Note
    MidiBindingMode mode; // Absolute, Toggle o Momentary
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

Los comandos 1–11 se consumen mediante `consumePendingMidiUiCommand()` porque
requieren componentes del editor.

**Comandos de control interno:**

| ID | Constante | Acción |
|----|-----------|--------|
| 12 | `midiUiCommandTransportPlayPause` | Play/pause |
| 13 | `midiUiCommandTransportStop` | Stop |
| 14 | `midiUiCommandTransportSeekBack` | Seek -5 s |
| 15 | `midiUiCommandTransportSeekForward` | Seek +5 s |
| 16 | `midiUiCommandTempoDown` | Tempo -1 BPM |
| 17 | `midiUiCommandTempoUp` | Tempo +1 BPM |
| 18 | `midiUiCommandPadsEnable` | Habilitar pads |
| 19 | `midiUiCommandPadsDisable` | Deshabilitar pads |
| 20 | `midiUiCommandPadsToggle` | Toggle pads |
| 21 | `midiUiCommandEnsurePadsPack` | Preparar pack de pads fuera de RT |

Los comandos 12–21 usan una FIFO SPSC fija de 64 entradas. Un timer de 100 Hz
propiedad del procesador los consume en orden en el message thread, incluso si
el editor está cerrado. Al ser una cola preasignada, publicar no toma locks ni
reserva memoria; si se llena, el evento se descarta y aumenta un contador
interno en vez de bloquear el callback.

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

`midi_settings` verifica persistencia de bindings y disparos de CC/Note en los
tres modos, incluidos los bordes de pulsación y liberación.

### Contrato realtime

El callback no muta el vector de bindings ni persiste el resultado de MIDI
Learn. Publica campos de tamaño fijo y el editor completa esas acciones.

Transporte, tempo y operaciones de pads que pueden tomar locks, preparar el
dispositivo o realizar I/O se convierten en comandos de control. Note-on/note-off
de pads se acumulan en bitmasks atómicos; el render los consume sólo después de
obtener el `tryLock` existente de `PadsEngine`. La carga del pack usa un request
atómico coalescido. El timer sólo despierta el mismo worker usado por la UI: la
descarga y la decodificación no se ejecutan en el message thread. Las notas MIDI
usan el inicio residente igual que los botones; el callback solicita completar
la nota activa mediante un flag atómico. Si la carga falla, descarta las notas
pendientes para impedir que se disparen mucho después sobre otro pack. El probe
deriva su timestamp de una calibración monotónica y no llama al reloj de pared
desde audio.

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
