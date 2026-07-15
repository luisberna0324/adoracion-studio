---
type: Audio Engine Class
title: TransportClock
description: Reloj de transporte frame-based lock-free
resource: src/audio/TransportClock.h
tags: [juce, audio, transport, clock, frame-based]
timestamp: 2026-07-14T20:45:00Z
---

# TransportClock

Reloj de transporte thread-safe basado en frames. Reemplazo moderno del sistema de transporte legacy basado en atomics.

## Namespace

```cpp
namespace playworship::audio {
```

## Tipos

### Frame

```cpp
using Frame = std::int64_t;
```

Representa posición en samples (frame de audio).

### State

```cpp
enum class State : int {
    stopped = 0,
    playing = 1,
    paused = 2
};
```

Estados del transporte.

## API Pública

### Lifecycle

```cpp
void prepare(double sampleRate) noexcept;
```

Configura el sample rate. Debe llamarse antes de usar.

```cpp
void reset(Frame frame = 0) noexcept;
```

Resetea a posición inicial.

### Control de transporte

```cpp
void play() noexcept;                    // Iniciar reproducción
void pause() noexcept;                   // Pausar
void stop(Frame returnFrame = 0) noexcept;  // Detener y retornar a frame
void seek(Frame frame) noexcept;         // Ir a posición
void advance(int frameCount) noexcept;  // Avanzar frames
```

### Estado

```cpp
State getState() const noexcept;
bool isPlaying() const noexcept;
Frame getPositionFrames() const noexcept;
double getPositionSeconds() noexcept;
double getSampleRate() const noexcept;
```

### Utilidades estáticas

```cpp
static Frame secondsToFrames(double seconds, double sampleRate) noexcept;
static double framesToSeconds(Frame frames, double sampleRate) noexcept;
```

Conversión entre segundos y frames.

## Atómicos internos

| Atómico | Tipo | Default | Descripción |
|---------|------|---------|-------------|
| `sampleRateHz` | `double` | 44100.0 | Sample rate en Hz |
| `positionFrames` | `Frame` | 0 | Posición actual |
| `state` | `int` | stopped | Estado del transporte |

## Ejemplo de uso

```cpp
TransportClock clock;

// Preparar para 48kHz
clock.prepare(48000.0);

// Reproducir desde inicio
clock.play();

// En callback de audio
void audioCallback(float *buffer, int numSamples) {
    if (clock.isPlaying()) {
        auto frames = clock.getPositionFrames();
        // ... process audio at position frames ...
        clock.advance(numSamples);
    }
}

// Seek a posición
auto targetFrame = TransportClock::secondsToFrames(30.0, 48000.0);
clock.seek(targetFrame);
```

## Comparación con sistema legacy

| Aspecto | Legacy | TransportClock |
|---------|--------|-----------------|
| Posición | `std::atomic<double>` | `std::atomic<Frame>` |
| Precisión | Double (segundos) | Int64 (frames exactos) |
| Drift | Posible con conversiones | Eliminado |
| API | `setNextReadPosition` | `seek/advance` |

## Archivos relacionados

- [SectionScheduler](SectionScheduler.md) - Usa TransportClock
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md) - Contiene TransportClock
