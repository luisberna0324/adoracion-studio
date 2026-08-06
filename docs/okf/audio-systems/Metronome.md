---
type: Audio System Class
title: Metronome
description: Motor de metrónomo configurable con samples personalizados y subdivisions
resource: src/MetronomeEngine.h
tags: [juce, audio, metronome, click, rhythm]
timestamp: 2026-07-25T00:00:00Z
---

# Metronome

Motor de metrónomo que puede usar samples de audio personalizados o synthesis.

## Conceptos clave

### Subdivisiones

El motor soporta tres niveles de subdivisión:

| Valor | Nombre | Descripción |
|-------|--------|-------------|
| 1 | Quarter (1/4) | Negra |
| 2 | Eighth (1/8) | Corchea |
| 4 | Sixteenth (1/16) | Semicorchea |

### Division Mask

Máscara de bits para controlar qué subdivisiones suenan:

```cpp
divisionMask = 1;     // Solo negras
divisionMask = 1 | 2;  // Negras y corcheas
divisionMask = 1 | 2 | 4;  // Todas
```

### Click Tones

| Tone | Uso |
|------|-----|
| `downbeat` | Primera nota del compás |
| `eighth` | Nota de corchea |
| `sixteenth` | Nota de semicorchea |

## API Pública

### Preparación

```cpp
void prepare(double sampleRate);
void reset();
void requestReset() noexcept;
```

`prepare()` configura el motor para el sample rate dado. `reset()` muta el
estado interno de fase, beat, voces y síntesis directamente; sólo es válido
cuando el motor está detenido o desde el audio thread.

Desde control se usa `requestReset()`: incrementa una generación atómica y no
toca estado audio-owned. La generación pendiente se consume únicamente en la
siguiente sincronización de fase (`syncPhaseForTimeSeconds()` o
`syncPhaseForBeatPosition()`). `process()` no consume resets para evitar que un
reset publicado entre sync y render vuelva la fase a cero dentro del mismo
callback. Varias solicitudes no se pierden ni requieren lock.

### Habilitación

```cpp
void setEnabled(bool enabled) noexcept;
bool isEnabled() const noexcept;
```

Activa/desactiva el metrónomo.

### Tempo

```cpp
void setBpm(double bpm) noexcept;
double getBpm() const noexcept;
```

Establece el tempo en BPM (20-300 típicamente).

### Compás

```cpp
void setBeatsPerBar(int beats) noexcept;
int getBeatsPerBar() const noexcept;
```

Define beats por compás (4 = 4/4, 3 = 3/4, etc).

### Volumen

```cpp
void setVolume(float volume) noexcept;  // 0.0 - 1.0
float getVolume() const noexcept;
```

Nivel de volumen.

### Mute

```cpp
void setMuted(bool muted) noexcept;
bool isMuted() const noexcept;
```

Silencia el metrónomo.

### Subdivisiones

```cpp
// Qué subdivisión se está usando (1, 2, o 4)
void setSubdivision(int subdiv) noexcept;
int getSubdivision() const noexcept;

// Máscara de bits de qué layers están activos
void setDivisionMask(int mask) noexcept;
int getDivisionMask() const noexcept;
```

`setSubdivision()` publica un valor persistente `requestedSubdivision`;
`getSubdivision()` devuelve ese valor solicitado. El audio thread conserva
`activeSubdivision` y adopta el último request sólo en el siguiente límite de
negra. De este modo un cambio de UI no desplaza la grilla a mitad de beat y el
audio thread nunca borra una publicación concurrente.

### Sincronización

```cpp
void syncPhaseForTimeSeconds(double seconds) noexcept;
void syncPhaseForBeatPosition(double beatPosition,
                              double effectiveBpm) noexcept;
```

`syncPhaseForTimeSeconds()` mantiene la API simple usando el BPM configurado.
La integración con transporte usa `syncPhaseForBeatPosition()`: publica en una
sola llamada la posición absoluta dentro de la grilla musical y el BPM efectivo
que debe avanzar audio. El processor calcula `beatPosition` desde segundos de
source y BPM original, y `effectiveBpm` desde el ratio de tempo audio-owned.
Así fase y velocidad no pueden pertenecer a dos targets de tempo distintos
dentro del mismo callback.

### Trigger manual

```cpp
void triggerImmediateClick() noexcept;
```

Fuerza un click inmediato (para preview).

### Procesamiento

```cpp
void process(juce::AudioBuffer<float> &buffer) noexcept;
void process(juce::AudioBuffer<float> &buffer,
             int startSample,
             int numSamples) noexcept;
```

La primera variante procesa todo el buffer. La segunda limita escritura y
avance de fase al rango indicado. El transporte la usa para renderizar el
prefijo con la fase anterior y el sufijo con la fase del destino cuando un loop
o jump cae dentro del bloque.

### Samples personalizados

```cpp
bool loadClickSamplesFromFolder(
    const juce::File &folder,
    const juce::String &style,
    juce::AudioFormatManager &formatManager,
    juce::String *errorMessage = nullptr
);

void clearClickSamples() noexcept;
bool hasClickSamples() const noexcept;
```

Carga samples de click desde carpeta. Busca archivos con sufijos:
- `*_downbeat.wav` - primera nota
- `*_quarter.wav` - negra
- `*_eighth.wav` - corchea
- `*_sixteenth.wav` - semicorchea

La publicación del set usa un puntero crudo atómico a una generación inmutable.
Las generaciones quedan retenidas por el control thread hasta destruir el
motor. `clearClickSamples()` retira el set para triggers nuevos, pero las voces
activas pueden terminar de leer memoria válida sin tomar locks ni destruir
buffers desde el audio thread.

## Estructura interna

### ClickSample

```cpp
struct ClickSample {
    juce::AudioBuffer<float> data;
    double sourceSampleRate = 44100.0;
    float normalizationGain = 1.0f;
};
```

### ClickSampleSet

```cpp
struct ClickSampleSet {
    juce::String style;           // Nombre del estilo
    ClickSample downbeat;
    ClickSample quarter;
    ClickSample eighth;
    ClickSample sixteenth;
};
```

### SampleVoice

```cpp
struct SampleVoice {
    const ClickSampleSet *sampleSet = nullptr;
    const ClickSample *sample = nullptr;
    double position = 0.0;       // Posición en sample
    double increment = 1.0;       // Incremento por frame
    float gain = 1.0f;
};
```

Hay 16 voces de sample para permitir overlaps.

`phase`, `subPhase`, `beatIndex`, `subIndex`, `activeBpm`,
`activeSubdivision`, las voces y `appliedResetGeneration` pertenecen
exclusivamente al audio thread. Control sólo escribe atomics de configuración,
generaciones y el puntero publicado a samples. La retención de
`ClickSampleSet` se hace fuera del callback.

## Click synthesis

Si no hay samples cargados, el motor sintetiza clicks:

```cpp
float renderSyntheticClick() noexcept;
```

Genera clicks usando síntesis simple (sine + envelope).

## Constantes internas

| Variable | Tipo | Default |
|----------|------|---------|
| `clickGain` | `float` | 0.22f |
| `sampleRate` | `double` | 44100.0 |
| `requestedSubdivision` | `atomic<int>` | 1 |
| `activeSubdivision` | `int` | 1 |
| `requestedResetGeneration` | `atomic<uint64_t>` | 0 |

## Ejemplo de uso

```cpp
MetronomeEngine metronome;
metronome.prepare(48000.0);

// Configurar tempo
metronome.setBpm(120.0);
metronome.setBeatsPerBar(4);

// Habilitar negras y semicorcheas
metronome.setDivisionMask(1 | 4);
metronome.setSubdivision(1);

// Cargar samples de click jazz
metronome.loadClickSamplesFromFolder(
    juce::File("/samples/clicks/jazz"),
    "jazz",
    formatManager
);

// Activar y procesar
metronome.setEnabled(true);

// En callback de audio
void audioCallback(AudioBuffer<float> &buffer) {
    // ... otros procesamiento ...
    metronome.process(buffer);
}
```

## Integración con MTControlsAudioProcessor

El processor tiene una API de más alto nivel:

```cpp
processor.setMetronomeEnabled(true);
processor.setMetronomeBpm(120.0);
processor.setMetronomeBeatsPerBar(4);
processor.setClickDivision(1 | 4);  // 1/4 + 1/16
```

## Archivos relacionados

- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md)
- [DynamicGuide](DynamicGuide.md) — comparte convención de default mono Out 1
