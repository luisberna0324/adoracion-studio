---
type: Audio Engine Class
title: StemMixEngine
description: Motor de mezcla de stems con procesamiento de pitch y tempo
resource: src/StemMixEngine.h
tags: [juce, audio, engine, mix, stem, pitch, tempo]
timestamp: 2026-07-14T20:45:00Z
---

# StemMixEngine

Motor de mezcla que combina múltiples stems con soporte para pitch shift y tempo scaling.

## Pipeline de procesamiento

```
Stems Input → Time Stretch (pitch) → Delay Comp → Mix → Fade Window → Output
                     ↓
              Percussion Detection
                     ↓
              Different processing paths
```

## Estructura interna

### Procesadores de tiempo

| Procesador | Propósito |
|-------------|-----------|
| `pitchShiftMelodic` | Time stretch para melodías |
| `tempoShiftPercussion` | Time stretch para percusión |
| `tempoShiftGuide` | Time stretch para guía |

### Delay lines (compensación de latencia)

| Delay | Propósito |
|-------|-----------|
| `pitchDelayMelodic` | Compensar latencia melódica |
| `pitchDelayPercussion` | Compensar latencia percusiva |
| `pitchDelayGuide` | Compensar latencia de guía |

### Buffers internos

| Buffer | Tamaño | Propósito |
|--------|--------|-----------|
| `stemMixBuffer` | variable | Mezcla de stems procesada |
| `stemTempBuffer` | variable | Buffer temporal |
| `percussionBuffer` | variable | Señal percusiva separada |
| `dryBuffer` | variable | Señal seca (sin procesar) |
| `processedBuffer` | variable | Señal procesada final |

## API Pública

### Preparación

```cpp
void prepare(double sampleRate, int samplesPerBlock, int numChannels);
```

Configura el motor para el sample rate y tamaño de bloque dados.

### Reset

```cpp
void reset();
```

Resetea todos los procesadores y buffers al estado inicial.

### Pitch shift

```cpp
void setPitchSemiTones(int semitones);
```

Establece el pitch shift en semitonos (-12 a +12).

### Tempo ratio

```cpp
void setTempoRatio(double ratio);
```

Establece la proporción de tempo (1.0 = normal, 0.5 = mitad, 2.0 = doble).

## Métodos de render

### Mix stems (con procesamiento)

```cpp
void mixStems(
    const std::vector<std::unique_ptr<StemTrack>> &stems,
    const std::atomic<bool> *clickGuideStates,
    const std::atomic<bool> *pitchShiftStates,
    int stemCount,
    int inputSamples,
    int numChannels
);
```

Mezcla stems aplicando:
- Click guide states para separación melódica/percusiva
- Pitch shift states para aplicar pitch shift por stem
- Time stretch según tempo ratio

### Render directo

```cpp
void renderDirect(
    const std::vector<std::unique_ptr<StemTrack>> &stems,
    int stemCount,
    juce::AudioBuffer<float> &output
);
```

Renderiza stems sin procesamiento de tiempo (para preview).

### Render a output

```cpp
void renderToOutput(
    juce::AudioBuffer<float> &output,
    bool timeProcessActive,
    int inputSamples,
    float fadeStart,
    float fadeEnd,
    float windowFadeStart,
    float windowFadeEnd
);
```

Renderiza a buffer de salida con fade window.

## Latencia

### Frames requeridos

```cpp
int requiredInputFrames(int outputFrames) noexcept;
```

Calcula frames de input necesarios para producir outputFrames (considera processing delay).

### Latencia de output

```cpp
int getOutputLatencySamples() const noexcept;
```

Retorna la latencia total en samples.

## Constantes

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `kMaxPitchLatencySamples` | 65536 | Máximo delay para pitch |

## Notas de implementación

### Separación melódica/percusiva

El motor detecta automáticamente qué stems son melódicos vs percusivos basándose en:
- `clickGuideStates`: qué stems contribuyen al click guide
- `pitchShiftStates`: qué stems tienen pitch shift activo

Los stems percusivos usan `tempoShiftPercussion` con latency diferente a los melódicos.

### Delay lines

Las delay lines (`SimpleDelayLine`) compensan la diferente latencia de procesamiento entre:
- Stems con time stretch (mayor latencia)
- Stems sin processing (menor latencia)

## Archivos relacionados

- [StemTrack](StemTrack.md) - Track individual de audio
- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md) - Usa StemMixEngine
