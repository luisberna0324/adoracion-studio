---
type: Audio System Class
title: Pads
description: Motor de pads con 12 notas y crossfade
resource: src/audio/PadsEngine.h
tags: [juce, audio, pads, samples, trigger]
timestamp: 2026-07-14T20:45:00Z
---

# PadsEngine

Motor de pads/samples que soporta 12 notas musicales con crossfade suave.

## Constantes

```cpp
static constexpr int kNoteCount = 12;
static constexpr const char *kNoteNames[kNoteCount] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
```

## Crossfade Presets

```cpp
enum class CrossfadePreset : int {
    Fast = 0,    // Crossfade rápido (live performance)
    Medium,      // Balance default
    Long         // Crossfade largo (suave)
};
```

## LoadResult

```cpp
struct LoadResult {
    bool ok = false;
    juce::String statusText;
    int loadedNotes = 0;
};
```

Resultado de cargar un pack de pads.

## API Pública

### Preparación

```cpp
void prepare(double sampleRate, int maxBlockSize);
void releaseResources();
```

Configura el motor.

### Habilitación

```cpp
void setEnabled(bool shouldEnable) noexcept;
bool isEnabled() const noexcept;
```

Activa/desactiva los pads.

### Crossfade

```cpp
void setCrossfadePreset(CrossfadePreset preset) noexcept;
CrossfadePreset getCrossfadePreset() const noexcept;
```

Cambia el preset de crossfade.

### Carga de samples

```cpp
LoadResult loadFromFolder(
    const juce::File &folder,
    juce::AudioFormatManager &formatManager
);
```

Carga samples desde carpeta. Busca archivos con nombres de notas:
- `C.wav`, `C#.wav`, `D.wav`, etc.
- `C.mp3`, `C#.flac`, etc.

### Trigger de notas

```cpp
void triggerNote(int noteIndex, int octave);
void stopNote(int noteIndex);
void stopAll();
```

- `noteIndex`: 0-11 (C a B)
- `octave`: Octava (0-8 típicamente)

### Procesamiento

```cpp
void process(juce::AudioBuffer<float> &outputBuffer);
```

Mezcla la salida de los pads en el buffer.

### Metering

```cpp
float getOutputLevel() const noexcept;
```

Retorna nivel de salida (0-1).

## Arquitectura de voces

### NoteSample

```cpp
struct NoteSample {
    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    bool valid = false;
};
```

Sample cargado para cada nota.

### Voice

```cpp
struct Voice {
    VoiceState state = VoiceState::stopped;
    int noteIndex = -1;
    int octave = 0;
    double position = 0.0;
    double increment = 1.0;
    float gain = 0.0f;
    float fadeOutStartGain = 0.0f;
    float fadeOutProgress = 0.0f;
};
```

Hay 12 voces (una por nota).

### VoiceState

```cpp
enum class VoiceState : int {
    stopped = 0,
    fadingIn,
    playing,
    fadingOut
};
```

### Crossfade durations

| Preset | Fade In | Fade Out |
|--------|--------|---------|
| Fast | ~10ms | ~50ms |
| Medium | ~50ms | ~200ms |
| Long | ~200ms | ~500ms |

## Algoritmo de trigger

Cuando se triggerea una nota:

1. Busca voz libre o con menor priority
2. Si todas activas, reclamar la más antigua
3. Iniciar fade-in desde gain=0
4. Configurar position=0, increment según octave

## Ejemplo de uso

```cpp
PadsEngine pads;
pads.prepare(48000.0, 512);
pads.setCrossfadePreset(PadsEngine::CrossfadePreset::Medium);

// Cargar samples
LoadResult result = pads.loadFromFolder(
    juce::File("/pads/acoustic"),
    formatManager
);

if (result.ok) {
    std::cout << "Loaded " << result.loadedNotes << " notes\n";
}

// Trigger C4 (octave 4, note 0)
pads.triggerNote(0, 4);  // C4

// Trigger E4
pads.triggerNote(4, 4);  // E4

// Detener después de 2 segundos
pads.stopNote(0);
```

## MIDI mapping

Los pads se mapean a notas MIDI:

```cpp
// noteIndex + (octave * 12) = MIDI note number
// C4 = 0 + (4 * 12) = 48

void midiNoteOn(int midiNote, int velocity) {
    int octave = midiNote / 12;
    int noteIndex = midiNote % 12;
    pads.triggerNote(noteIndex, octave);
}

void midiNoteOff(int midiNote) {
    int octave = midiNote / 12;
    int noteIndex = midiNote % 12;
    pads.stopNote(noteIndex);
}
```

## API de alto nivel (MTControlsAudioProcessor)

```cpp
processor.setPadsEnabled(true);
processor.setPadsCrossfadePreset(PadsEngine::CrossfadePreset::Long);
processor.triggerPadNote(0, 4);  // C4
processor.stopPadNote(0);
```

## Archivos relacionados

- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
