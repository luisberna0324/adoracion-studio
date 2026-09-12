---
type: Audio System Class
title: Pads
description: Motor de pads con 12 notas, seguimiento de tonalidad y loop continuo
resource: src/audio/PadsEngine.h
tags: [juce, audio, pads, samples, trigger]
timestamp: 2026-08-06T00:00:00Z
---

# PadsEngine

Motor de pads/samples que soporta 12 notas musicales con crossfade suave y
reproducción continua. Un pad activo no depende del transporte de la canción.

## Constantes

```cpp
static constexpr int kNoteCount = 12;
static constexpr const char *kNoteNames[kNoteCount] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
```

`noteIndexForSongKey()` normaliza tonalidades mayores o menores (`C`, `F#`,
`Bb`, `Am`, `Key of C#m`) a esos doce índices. Si el texto no contiene una
tonalidad reconocible retorna `-1` y el pad activo no cambia.

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
double getCrossfadeDurationSeconds() const noexcept;
void beginSongTransitionFadeOut() noexcept;
void beginSongTransitionFadeIn() noexcept;
```

Cambia el preset de crossfade. La misma duración se aplica al cambio de nota,
incluido el cambio automático de tonalidad entre canciones.

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

La carga del pack debe tratarse como trabajo pesado de I/O y decodificación.
`loadFromFolder()` indexa y valida los 12 archivos. Al iniciar el procesador,
`startPadsPreloadAsync()` prepara el pack y decodifica las doce notas mediante
un worker dedicado. No bloquea el message thread y nunca realiza I/O desde el
hilo de audio. Los buffers permanecen residentes durante la vida del procesador.
`MTControlsAudioProcessor::ensurePadsPackReady()` coalesce solicitudes mediante
un flag atómico para que sólo una preparación pueda correr a la vez.

Si la UI solicita una nota antes de completar la precarga, el procesador conserva
la última nota y octava solicitadas y la dispara automáticamente cuando los
buffers quedan listos. La UI consulta `isPadsPreloadRunning()` para representar
el estado, pero no posee ni espera al worker. Los triggers MIDI mantienen además
su mecanismo de reintento por máscara.

### Trigger de notas

```cpp
void triggerNote(int noteIndex, int octave);
void triggerNoteForSongTransition(int noteIndex, int octave);
void stopNote(int noteIndex);
void stopAll();
```

- `noteIndex`: 0-11 (C a B)
- `octave`: Octava (0-8 típicamente)

`triggerNoteForSongTransition()` se conserva por compatibilidad y tiene la
misma semántica continua que `triggerNote()`.

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
| Fast | 1 s | 1 s |
| Medium | 3 s | 3 s |
| Long | 5 s | 5 s |

## Loop continuo

Cada voz vuelve a reproducirse al alcanzar el final del sample. Para evitar un
corte audible, los últimos dos segundos mezclan progresivamente el inicio del
archivo; al envolver, el cursor salta después de la porción inicial que ya fue
renderizada por el crossfade. En archivos muy cortos se limita el solapamiento
a una cuarta parte de su duración; por debajo de ocho muestras se usa el loop
directo.

El loop continúa aunque termine o cambie la canción. Sólo se detiene al pulsar
de nuevo la misma nota, apagar Pads o llamar `stopNote()`/`stopAll()`.

## Seguimiento de tonalidad entre canciones

La UI conserva la nota seleccionada como estado global. Cuando se carga otra
canción y existe un pad activo, convierte la tonalidad de la canción —incluido
el desplazamiento de pitch guardado— al índice cromático y dispara la nota
nueva. `triggerNote()` aplica el preset de crossfade a la voz saliente y a la
entrante. Si no hay pad activo o la tonalidad no se reconoce, no se inicia ni
se modifica ningún pad. `getActiveNoteIndex()` publica el pad seleccionado para
que este comportamiento también funcione cuando la nota se disparó por MIDI y
para que la interfaz mantenga su indicador sincronizado.

Cuando la canción siguiente tiene una tonalidad distinta, la UI dispara la
nueva nota al completar la carga de la canción. `PadsEngine::triggerNote()`
mantiene la voz anterior mientras la nueva entra durante los 1, 3 o 5 segundos
configurados. Por tanto el bus de Pads no se baja a silencio ni se demora la
carga de la canción. Si ambas tonalidades coinciden, se conserva la voz y la
posición de loop actual; si la tonalidad de destino no puede resolverse, el pad
activo permanece sin cambios. La misma regla se usa para siguiente/anterior,
biblioteca y autoplay.

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
