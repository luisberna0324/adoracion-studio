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

### Anuncios de guía dinámica

Las secciones musicales (`Intro`, `Verse`, `Pre-Chorus`, `Chorus`,
`Refrain`, `Post-Chorus`, `Bridge`, `Instrumental`, `Solo`, `Breakdown`,
`Interlude`, `Tag`, `Vamp`, `Outro` y `Ending`) se anuncian un compás antes
de su entrada. El compás se calcula con el BPM y la métrica activa de la
canción.

Los conteos y las utilidades se anuncian al entrar en su sección. Las
utilidades incluyen indicaciones como cambios de intensidad, pausas,
sostener, entradas de instrumentos, cambios de tono, última vez y final
grande. Una sección que comienza en `0.0 s` se anuncia al entrar porque no
hay un compás anterior disponible.

El scheduler vive en el timeline fuente. Por eso, cuando cambia el tempo, el
adelanto de un compás se convierte con
`(60 / playbackBpm) * beatsPerBar * tempoRatio`. Cambiar BPM o métrica vuelve a
publicar el snapshot para mantener el mismo compás audible que el click y el
bounce offline.

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

`Stop` siempre deja el transporte detenido en `0.0 s`. El inicio de la primera
sección del arrange y el inicio configurado de la ventana de reproducción no
alteran esa posición de retorno. Si no existe una sección que cubra `0.0 s`, no
queda ninguna sección marcada como actual.

### Edición

```cpp
void setArrangementEditingActive(bool editingActive) noexcept;
```

Marca si el usuario está editando el arrange.

Mientras la edición está activa, publicar un snapshot mediante
`setSongArrangement()` conserva el estado y la posición actuales del
transporte. Crear, eliminar, mover o redimensionar una sección actualiza el
scheduler en el lugar y solo realinea la lectura de source si el nuevo mapeo
afecta al playhead; no detiene ni rebobina la canción. Los loops y saltos
pendientes se conservan por ID mientras su sección siga existiendo.

La marca de inserción de la regla aplica a todos los tipos de sección,
incluidas `Cuenta` y `Utilidad`. Estas dos conservan su duración corta de grilla
y dividen la sección existente alrededor de la marca sin crear solapamientos.

En el editor de guía, si no hay secciones se muestra un único botón `+` centrado
sobre la waveform. Con secciones, el mismo `+` aparece como cuarta acción en la
barra contextual de cada sección y abre el selector contextual. La lista inicial
muestra directamente todas las secciones; las secciones con variantes (por
ejemplo `Verso` o `Coro`) abren su propio submenú. `Conteos` y `Utilidades`
también se abren como submenús en cascada. Al abrirlo desde una sección, la
inserción usa su límite final; al seleccionar una opción se cierra toda la
cadena de menús. El botón no reserva altura adicional ni reduce el área de
waveform.

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
    const std::function<void(double progress01)> &onLoadProgress = {},
    bool deferTransitionFadeInUntilConfigured = false
);

void finishPendingSongTransition();
```

## Transición entre canciones

Cuando se carga otra canción durante playback, el transporte usa una transición
`fade-through-zero`:

1. La canción actual continúa mientras se crean y prebufferizan los stems nuevos.
2. El output completo baja de 1 a 0 en 100 ms.
3. El reemplazo de fuentes ocurre únicamente con el output en silencio.
4. La canción nueva queda pausada y en gain 0 mientras se restaura su snapshot.
5. `finishPendingSongTransition()` inicia playback y sube de 0 a 1 en 150 ms.

La suspensión breve necesaria para reemplazar los contenedores se realiza solo
después de alcanzar cero. Por tanto, cualquier buffer vacío del host coincide
con una ventana que ya es silenciosa.

Si `deferTransitionFadeInUntilConfigured` es `false`, `loadSongStems()` completa
automáticamente el fade-in. La UI principal usa `true` para aplicar antes del
fade-in mezcla, routing, tempo, click, guía, ventana y arrangement.

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

### Cambio seguro a la siguiente canción

```cpp
// Preparar y reemplazar manteniendo la entrada en silencio.
processor.loadSongStems(nextSongPaths, nextTitle, nextKey, nextBpm,
    [](double p) { /* progress */ }, true);

// Restaurar aquí mix/routing/click/guía/arrangement.
processor.finishPendingSongTransition();
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
