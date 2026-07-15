---
type: Transport API
title: Transport
description: Control de transporte y arrange en MTControlsAudioProcessor
resource: src/PluginProcessor.h
tags: [juce, audio, transport, playback, arrange]
timestamp: 2026-07-14T20:45:00Z
---

# Transport API

Control de reproducción y arrange musical.

## Conceptos

### Transport States

```cpp
enum class TransportState : int {
    stopped = 0,
    playing = 1,
    paused = 2
};
```

### Sistema dual

El plugin usa dos sistemas de transporte:
1. **Legacy** (atomics): Para UI mirroring
2. **Frame-based** (TransportClock): Source of truth para audio thread

Durante la migración a engine-v2, ambos coexisten.

## Transport Controls

### Reproducción

```cpp
void playSong();           // Iniciar
void pauseSong();          // Pausar
void stopSong();           // Detener y resetear
void toggleSongPlayPause(); // Toggle
```

### Posición

```cpp
// Seek
void seekSongTo(double positionSeconds);
void seekSongRelative(double deltaSeconds);

// Configuración de loop region
void setSongStartSeconds(double seconds) noexcept;
double getSongStartSeconds() const noexcept;
void setSongEndSeconds(double seconds) noexcept;
double getSongEndSeconds() const noexcept;
```

### Consulta de estado

```cpp
bool isSongPlaying() const noexcept;
bool hasLoadedSong() const noexcept;
double getSongCurrentPositionSeconds() const;
double getSongLengthSeconds() const;
```

## Fade

### Fade out

```cpp
void setSongFadeOutSeconds(double seconds) noexcept;
double getSongFadeOutSeconds() const noexcept;
void setFadeActive(bool shouldFadeOut) noexcept;
bool isFadeActive() const noexcept;
```

### Fade behavior

```cpp
void setFadeStopsPlayback(bool shouldStop) noexcept;
bool isFadeStopsPlayback() const noexcept;
```

Si `fadeStopsPlayback = true`, al terminar el fade se pausa.
Si `false`, sigue reproduciendo silenciosamente.

## Tempo

### Playback tempo

```cpp
void setPlaybackTempoBpm(double bpm) noexcept;
double getPlaybackTempoBpm() const noexcept;
double getOriginalSongBpm() const noexcept;
```

Cambia el tempo de reproducción sin alterar el pitch (time stretch).

## Arrangement

### Concepto

El arrange permite definir secciones no lineales:
- Saltar entre partes
- Hacer loops de secciones
- Reordenar playback

### Snapshots

```cpp
void setSongArrangement(
    const SongArrangementSnapshot &arrangement,
    bool isCustomArrangement
);
bool isCustomArrangementActive() const noexcept;
```

### Part jumps

```cpp
void requestArrangementPartJump(int partIndex);
int getPendingArrangementPartJumpIndex() const noexcept;
void clearPendingArrangementPartJump() noexcept;
```

Pide saltar a una parte específica en el próximo ciclo.

### Loops

```cpp
void toggleArrangementPartLoop(int partIndex);
void clearArrangementPartLoop() noexcept;
int getArrangementPartLoopIndex() const noexcept;
```

Activa loop en una parte específica.

### Edición

```cpp
void setArrangementEditingActive(bool editingActive) noexcept;
```

Marca si el usuario está editando el arrange.

## Song Metadata

### Información de canción cargada

```cpp
juce::String getLoadedSongTitle() const;
juce::String getLoadedSongKey() const;
double getLoadedSongBpm() const;
int getLoadedStemCount() const noexcept;
```

### Carga de canción

```cpp
bool loadSongStems(
    const juce::StringArray &stemPaths,
    const juce::String &songTitle,
    const juce::String &songKey,
    double songBpm,
    const std::function<void(double progress01)> &onLoadProgress = {}
);
```

## Crossfade entre canciones

El sistema soporta crossfade a otra canción:

```cpp
std::atomic<bool> songCrossfadeActive{false};
std::atomic<float> songCrossfadeProgress{1.0f};
```

Durante crossfade:
- `songCrossfadeIncomingGain` aumenta de 0 a 1
- `songCrossfadeOutgoingGain` disminuye de 1 a 0

## Seek lock-free

El seek usa atomics para ser thread-safe:

```cpp
std::atomic<bool> seekPending{false};
std::atomic<double> seekTargetSeconds{0.0};
```

### Algoritmo de seek

1. UI escribe `seekTargetSeconds` y `seekPending = true`
2. Audio thread detecta `seekPending`
3. Audio thread calcula frame y hace seek
4. Audio thread marca `seekPending = false`

## TransportClock (engine-v2)

Namespace: `playworship::audio`

### Estados

```cpp
enum class State : int {
    stopped = 0,
    playing = 1,
    paused = 2
};
```

### API

```cpp
void play() noexcept;
void pause() noexcept;
void stop(Frame returnFrame = 0) noexcept;
void seek(Frame frame) noexcept;
void advance(int frameCount) noexcept;

State getState() const noexcept;
bool isPlaying() const noexcept;
Frame getPositionFrames() const noexcept;
double getPositionSeconds() const noexcept;
```

## Ejemplo de uso

### Reproducción básica

```cpp
processor.playSong();

// Durante playback
auto position = processor.getSongCurrentPositionSeconds();
auto length = processor.getSongLengthSeconds();
float progress = position / length;
```

### Loop de sección

```cpp
// Configurar loop region
processor.setSongStartSeconds(30.0);
processor.setSongEndSeconds(60.0);

// Iniciar playback
processor.playSong();
```

### Crossfade a siguiente canción

```cpp
// Iniciar crossfade
processor.loadSongStems(nextSongPaths, nextTitle, nextKey, nextBpm,
    [](double p) { /* progress */ });

// Cuando lista, hacer crossfade
processor.setSongFadeOutSeconds(2.5);
processor.setFadeActive(true);
processor.playSong();  // Nueva canción entra con fade-in
```

### Arrangement

```cpp
// Definir arrange
SongArrangementSnapshot arrangement;
arrangement.parts = {
    { .start = 0.0, .end = 10.0, .loop = false },
    { .start = 10.0, .end = 30.0, .loop = true },  // Loop verse
    { .start = 30.0, .end = 50.0, .loop = false }
};

processor.setSongArrangement(arrangement, true);
processor.playSong();

// Saltar a chorus
processor.requestArrangementPartJump(2);
```

## Archivos relacionados

- [TransportClock](../../engine/TransportClock.md)
- [SectionScheduler](../../engine/SectionScheduler.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
