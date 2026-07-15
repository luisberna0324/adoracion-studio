---
type: Audio Engine Class
title: SectionScheduler
description: Programador de secciones de arrange con soporte para loops y jumps
resource: src/audio/SectionScheduler.h
tags: [juce, audio, arrange, section, scheduler]
timestamp: 2026-07-14T20:45:00Z
---

# SectionScheduler

Gestiona secciones de arrange musical. Permite definir partes, loops, jumps y transiciones entre secciones.

## Namespace

```cpp
namespace playworship::audio {
```

## Estructuras

### Section

```cpp
struct Section {
    Frame startFrame = 0;           // Inicio en frames de arrange
    Frame endFrame = 0;             // Fin en frames de arrange
    Frame sourceStartFrame = 0;     // Inicio en frames de source
    Frame sourceEndFrame = 0;       // Fin en frames de source
    Frame guideTriggerFrame = -1;   // Frame para trigger de guía
    int guideTriggerId = -1;        // ID del trigger de guía
    bool triggerGuideOnEntry = false;  // Trigger guía al entrar
    bool insertedClip = false;      // Es clip insertado?
    bool muteInstruments = false;    // Silenciar instrumentos
    bool skipPlayback = false;      // Saltar playback
};
```

Representa una sección del arrange.

### BlockDecision

```cpp
struct BlockDecision {
    int currentSection = -1;        // Sección actual
    bool transition = false;       // Hay transición
    bool stop = false;              // Detener playback
    bool skippedSection = false;    // Sección saltada
    int targetSection = -1;        // Sección destino
    Frame targetFrame = 0;          // Frame destino
};
```

Resultado de evaluación de bloque.

## Constantes

```cpp
static constexpr int maxSections = 256;
```

Máximo número de secciones soportadas.

## API Pública

### Publicación de secciones

```cpp
void publish(const std::vector<Section> &sections) noexcept;
```

Publica un nuevo arrange. Thread-safe.

```cpp
void clear() noexcept;
```

Limpia todas las secciones.

### Consulta

```cpp
int size() const noexcept;  // Número de secciones
```

Retorna cuántas secciones hay.

```cpp
int findSection(Frame frame) const noexcept;
```

Encuentra la sección que contiene el frame dado.

```cpp
int findGuideTrigger(Frame blockStart, int blockFrames) const noexcept;
```

Busca triggers de guía en el rango de frames.

```cpp
bool getSection(int index, Section &section) const noexcept;
```

Obtiene sección por índice.

```cpp
bool mapFrameToSourceFrame(
    Frame frame,
    Frame arrangementEndFrame,
    Frame sourceEndFrame,
    Frame &sourceFrameOut
) const noexcept;
```

Mapea frame de arrange a frame de source.

### Evaluación

```cpp
BlockDecision evaluate(
    Frame blockStart,
    int blockFrames,
    bool customArrangement,
    int pendingSection,
    int loopSection
) const noexcept;
```

Evalúa el bloque actual y decide qué hacer (play, loop, jump, stop).

## Arquitectura lock-free

### Snapshots

Usa triple-buffering para snapshots:

```cpp
struct Snapshot {
    int count = 0;
    std::array<Section, maxSections> sections{};
};

std::array<Snapshot, 3> snapshots{};
std::atomic<int> publishedIndex{0};
mutable std::atomic<int> readerIndex{-1};
```

- **Writer** (UI thread): Escribe en `snapshots[publishedIndex]`
- **Reader** (Audio thread): Lee desde `snapshots[readerIndex]`
- **Atomic swap**: Cambio atómico de índice de publicación

### Mutex interno

```cpp
mutable std::mutex publishMutex;
```

Protege la publicación de secciones.

## Ejemplo de uso

```cpp
SectionScheduler scheduler;

// Definir arrange con 3 secciones
std::vector<Section> sections = {
    // Intro (0-10s)
    { .startFrame = 0, .endFrame = 441000,
      .sourceStartFrame = 0, .sourceEndFrame = 441000 },
    
    // Verse (10-30s) 
    { .startFrame = 441000, .endFrame = 1323000,
      .sourceStartFrame = 441000, .sourceEndFrame = 1323000 },
    
    // Chorus con loop (30-50s)
    { .startFrame = 1323000, .endFrame = 2205000,
      .sourceStartFrame = 1323000, .sourceEndFrame = 1764000,
      .triggerGuideOnEntry = true, .guideTriggerId = 1 }
};

scheduler.publish(sections);

// En callback de audio
BlockDecision decision = scheduler.evaluate(
    currentFrame,
    blockFrames,
    useCustomArrangement,
    pendingJumpSection,
    loopSection
);

if (decision.transition) {
    // Ir a decision.targetFrame
}
```

## Casos de uso

### Loop de sección

1. Establecer `loopSection` en evaluate
2. Cuando el playback alcanza el final de la sección, salta a `startFrame`

### Jump a sección

1. Establecer `pendingSection` en evaluate
2. En el siguiente bloque, salta al inicio de esa sección

### Skip playback

1. Marcar `skipPlayback = true` en la sección
2. El scheduler salta directamente al final

## Archivos relacionados

- [TransportClock](TransportClock.md) - Proveedor de frames
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md) - Usa SectionScheduler
