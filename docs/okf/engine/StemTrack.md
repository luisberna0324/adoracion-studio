---
type: Audio Source Class
title: StemTrack
description: Track individual de audio con buffering y resampling lock-free
resource: src/StemTrack.h
tags: [juce, audio, stem, source, buffering, resampling]
timestamp: 2026-07-14T20:45:00Z
---

# StemTrack

Representa un stem individual de audio. Extiende `PositionableAudioSource` con pipeline híbrida de streaming.

## Pipeline

```
AudioFormatReader
       ↓
AudioFormatReaderSource
       ↓
StemBufferingSource (read-ahead buffer)
       ↓
ResamplingPositionableSource
       ↓
PositionableAudioSource (interface)
```

## Constructores

```cpp
StemTrack(
    std::unique_ptr<juce::AudioFormatReader> reader,
    juce::TimeSliceThread &sharedReadThread,
    double sampleRateToUse,
    int readAheadSamples = 262144
);
```

- `reader`: Reader del archivo de audio
- `sharedReadThread`: Thread compartido para lectura async
- `sampleRateToUse`: Sample rate destino
- `readAheadSamples`: Tamaño del buffer de read-ahead (default 256KB)

## PositionableAudioSource API

### Posición

```cpp
void setNextReadPosition(juce::int64 newPosition) override;
juce::int64 getNextReadPosition() const override;
juce::int64 getTotalLength() const override;
bool isLooping() const override;
void setLooping(bool shouldLoop) override;
bool waitForNextAudioBlockReady(int numSamples, juce::uint32 timeoutMs) const;
```

`waitForNextAudioBlockReady()` se usa fuera del audio thread para impedir que
una canción entre antes de que todos sus stems puedan entregar el primer bloque.
Un seek redundante a la posición actual conserva el read-ahead válido.

### Lifecycle

```cpp
void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
void releaseResources() override;
void getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
```

## Lock-Free Mix Controls

Todos los controles de mezcla son **lock-free** usando `std::atomic`. Son seguros para escribir desde UI thread y leer desde audio thread sin locks.

### Volumen

```cpp
void setVolume(float newVolume) noexcept;  // 0.0 - 1.0
float getVolume() const noexcept;
```

Volumen visual (0-1). Usado para faders de UI.

### Trim

```cpp
void setTrim(float newTrim) noexcept;       // usualmente 0.0 - 2.0
float getTrim() const noexcept;
```

Ajuste fino de ganancia.

### Mute

```cpp
void setMuted(bool shouldMute) noexcept;
bool isMuted() const noexcept;
```

Silencia el track.

### Solo

```cpp
void setSoloed(bool shouldSolo) noexcept;
bool isSoloed() const noexcept;
```

Aísla el track para escucha individual.

### Estado global de mezcla

```cpp
void setGlobalMixState(float mixGain, bool anySolo) noexcept;
```

Establece:
- `mixGain`: Ganancia combinada del mixer
- `anySolo`: Si algún track está en solo

## Magnitud y diagnóstico

### Magnitud actual

```cpp
float getMagnitude() const noexcept;
```

Retorna la magnitud RMS actual del buffer (para metering).

### Read misses

```cpp
juce::int64 getRealtimeReadMissCount() const noexcept;
```

Contador de "misses" del buffer de read-ahead. Un valor alto indica que el disco no puede mantener el streaming.

## Detalles de implementación

### Thread de lectura

La lectura de disco ocurre en `sharedReadThread` (TimeSliceThread). Esto mantiene el audio thread libre de operaciones de I/O.

### Resampling

`ResamplingPositionableSource` maneja conversión de sample rate si el archivo tiene diferente rate que `sampleRateToUse`.

### Buffering

`StemBufferingSource` implementa un buffer circular de read-ahead para absorber jitter de disco.

## Uso típico

```cpp
// Crear track desde archivo
auto reader = formatManager.createReaderFor(file);
auto track = std::make_unique<StemTrack>(
    std::move(reader),
    readThread,
    44100.0,
    262144
);

// Preparar para playback
track->prepareToPlay(512, 44100.0);
track->setLooping(true);

// En cada bloque de audio
juce::AudioSourceChannelInfo info(buffer, startSample, numSamples);
track->getNextAudioBlock(info);

// Controles de mezcla (thread-safe)
track->setVolume(0.8f);
track->setMuted(false);
track->setSoloed(true);
```

## Constantes internas

| Atómico | Tipo | Default | Descripción |
|---------|------|---------|-------------|
| `volume` | `float` | 1.0f | Volumen del track |
| `trim` | `float` | 1.0f | Trim del track |
| `muted` | `bool` | false | Mute state |
| `soloed` | `bool` | false | Solo state |
| `globalMixGain` | `float` | 1.0f | Ganancia global del mixer |
| `isAnySoloActive` | `bool` | false | Hay algún solo activo |
| `currentMagnitude` | `float` | 0.0f | Magnitud RMS actual |

## Archivos relacionados

- [StemMixEngine](StemMixEngine.md) - Motor que consume StemTracks
- [StemBufferingSource](StemBufferingSource.md) - Componente de buffering
- [ResamplingPositionableSource](ResamplingPositionableSource.md) - Componente de resampling
