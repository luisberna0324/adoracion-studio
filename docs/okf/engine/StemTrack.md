---
type: Audio Source Class
title: StemTrack
description: Track individual con resampling y read-ahead no bloqueante
resource: src/StemTrack.h
tags: [juce, audio, stem, source, buffering, resampling]
timestamp: 2026-07-25T00:00:00Z
---

# StemTrack

Representa un stem individual de audio. Extiende `PositionableAudioSource` con pipeline híbrida de streaming.

## Pipeline

```
AudioFormatReader
       ↓
AudioFormatReaderSource
       ↓
ResamplingPositionableSource
       ↓
StemBufferingSource (read-ahead buffer)
       ↓
PositionableAudioSource (interface)
```

## Constructores

```cpp
StemTrack(
    std::unique_ptr<juce::AudioFormatReader> reader,
    juce::TimeSliceThread &sharedReadThread,
    double sampleRateToUse,
    int readAheadSamples = 262144,
    bool useReadAhead = true,
    std::unique_ptr<juce::AudioFormatReader> prefetchReader = {}
);
```

- `reader`: Reader del archivo de audio
- `sharedReadThread`: Thread compartido para lectura async
- `sampleRateToUse`: Sample rate destino
- `readAheadSamples`: Tamaño del buffer de read-ahead en frames (default
  262144)
- `useReadAhead`: Usa streaming asíncrono; el bounce offline lo desactiva para
  lecturas síncronas deterministas
- `prefetchReader`: Segundo reader del mismo archivo para preparar destinos de
  seek/loop/jump sin mover ni resetear la cadena principal

## PositionableAudioSource API

### Posición

```cpp
void setNextReadPosition(juce::int64 newPosition) override;
juce::int64 getNextReadPosition() const override;
juce::int64 getTotalLength() const override;
bool isLooping() const override;
void setLooping(bool shouldLoop) override;
bool waitForNextAudioBlockReady(int numSamples, juce::uint32 timeoutMs) const;
void requestPrefetchPosition(juce::int64 position, int minimumFrames) noexcept;
bool isPrefetchReady(juce::int64 position, int minimumFrames) const noexcept;
void scheduleReadDiscontinuity(
    int prefixFrames,
    juce::int64 targetFrame,
    int fadeFrames = 64
) noexcept;
```

`waitForNextAudioBlockReady()` se usa fuera del audio thread para impedir que
una canción entre antes de que todos sus stems puedan entregar el primer bloque.
Un seek redundante a la posición actual conserva el read-ahead válido.

`requestPrefetchPosition()` e `isPrefetchReady()` exponen el banco secundario
del source. `scheduleReadDiscontinuity()` es exclusivamente audio-thread y arma
una única lectura compuesta sin reservar memoria:

```text
[prefixFrames del cursor actual][sufijo desde targetFrame]
```

El cambio de fuente ocurre en el offset exacto entregado por
`SectionScheduler`. Alrededor del límite se aplica un
`fade-through-zero` equal-power de hasta 64 frames: el final del prefijo usa
coseno y el inicio del sufijo seno. No es una superposición de dos voces; evita
el salto duro de forma de onda manteniendo intacta la duración del bloque.
`setNextReadPosition()` cancela cualquier discontinuidad todavía no consumida.

Los límites del callback tienen el mismo tratamiento sin reservar una segunda
voz:

- con `prefixFrames == 0`, el destino comienza en este bloque con un fade-in
  seno de hasta `fadeFrames`;
- con `prefixFrames == numSamples`, el bloque actual termina con un fade-out
  coseno y el fade-in del destino queda pendiente para el inicio del próximo
  bloque;
- `prepareToPlay()`, `releaseResources()` y un seek explícito cancelan ese
  estado pendiente.

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

El componente interno `StemBufferingSource` también mantiene
`getRealtimeMissingFrameCount()`, que acumula la cantidad exacta de frames
reemplazados por silencio. Cada callback vacío o parcial avanza siempre el
bloque completo para conservar la sincronización con el transporte y los demás
stems.

Durante un loop o jump, el prefijo y el sufijo son dos lecturas parciales del
mismo callback. Por tanto cada stem avanza exactamente `prefixFrames` en la
línea anterior y exactamente el largo del sufijo desde el destino, sin una
lectura completa adicional.

## Detalles de implementación

### Thread de lectura

La lectura de disco ocurre en `sharedReadThread` (TimeSliceThread). Esto mantiene el audio thread libre de operaciones de I/O.

### Resampling

`ResamplingPositionableSource` maneja conversión de sample rate si el archivo
tiene diferente rate que `sampleRateToUse`. En reproducción con prefetch hay
dos instancias independientes: una para el ring secuencial y otra para el banco
de destino. El preroll secundario nunca modifica la historia del resampler que
está sonando.

### Buffering

`StemBufferingSource` implementa un buffer circular de read-ahead para absorber
jitter de disco. El callback usa únicamente `tryEnter()` para la copia corta:
si el rango no está listo o el lock está ocupado, entrega silencio, registra el
miss y no espera al worker.

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

### Origen `.pwproj`

Cuando una canción se abre desde un proyecto portable `.pwproj`, `StemTrack`
no lee el ZIP directamente. `ProjectStore` extrae cada recurso validado a su
directorio de caché y el constructor recibe esa ruta temporal como cualquier
otro archivo de audio. Así se preservan el read-ahead, el prefetch y las
garantías de no bloquear el callback; la caché puede eliminarse y se recreará
al abrir el proyecto nuevamente.

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
