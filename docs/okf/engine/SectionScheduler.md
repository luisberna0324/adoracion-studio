---
type: Audio Engine Class
title: SectionScheduler
description: Programador de secciones de arrange con soporte para loops y jumps
resource: src/audio/SectionScheduler.h
tags: [juce, audio, arrange, section, scheduler]
timestamp: 2026-07-25T00:00:00Z
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
    int transitionOffsetFrames = -1; // Offset exacto dentro del bloque
};
```

Resultado de evaluación de bloque. Cuando existe una transición,
`transitionOffsetFrames` está en el rango inclusivo `[0, blockFrames]`:

- `0`: el salto ocurre antes de renderizar el primer frame, como un `skip`.
- `blockFrames`: el salto cae inmediatamente después del bloque.
- Un valor intermedio divide el bloque en prefijo y sufijo.
- `-1`: no existe transición.

Ejemplo: si una sección termina en el frame `1000` y el callback evalúa
`[512, 1024)`, el offset es `488`. El renderer puede entregar primero los 488
frames de la sección actual y completar los 24 restantes desde el destino.

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

### Snapshot único por callback

```cpp
struct Snapshot {
    std::uint64_t generation = 0;
    int count = 0;
    std::array<Section, maxSections> sections{};
    bool valid = false;
};

Snapshot acquireSnapshot() const noexcept;
```

`Snapshot` tiene tamaño fijo y no reserva memoria. El callback debe adquirirlo
una vez al comienzo del bloque y reutilizarlo en todas las consultas:

```cpp
SectionScheduler::Snapshot audioSnapshot;
const auto next = scheduler.acquireSnapshot();
if (next.valid)
    audioSnapshot = next;

const int section = SectionScheduler::findSection(audioSnapshot, currentFrame);
const auto decision = SectionScheduler::evaluate(
    audioSnapshot, currentFrame, blockFrames, custom, pending, loop);
```

Si una publicación excepcionalmente rápida impide obtener una generación
coherente durante los tres intentos acotados, `valid` es `false`. El callback no
espera ni gira indefinidamente: conserva su snapshot válido anterior.

### Helpers sobre snapshot

```cpp
static int size(const Snapshot &snapshot) noexcept;
static int findSection(const Snapshot &snapshot, Frame frame) noexcept;
static int findGuideTrigger(
    const Snapshot &snapshot,
    Frame blockStart,
    int blockFrames
) noexcept;
static bool getSection(
    const Snapshot &snapshot,
    int index,
    Section &section
) noexcept;
static bool mapFrameToSourceFrame(
    const Snapshot &snapshot,
    Frame frame,
    Frame arrangementEndFrame,
    Frame sourceEndFrame,
    Frame &sourceFrameOut
) noexcept;
static BlockDecision evaluate(
    const Snapshot &snapshot,
    Frame blockStart,
    int blockFrames,
    bool customArrangement,
    int pendingSection,
    int loopSection
) noexcept;
```

Las sobrecargas históricas sin parámetro `Snapshot` siguen disponibles por
compatibilidad. Cada una adquiere su propia generación; por ello no deben
encadenarse dentro de un callback que necesita una vista coherente.

`evaluate()` también marca transiciones implícitas en custom arrange cuando dos
secciones contiguas no son continuas en la fuente
(`current.sourceEndFrame != next.sourceStartFrame`). Esto cubre entrada y
salida de clips insertados aunque no exista loop ni jump explícito. La
transición usa `transitionOffsetFrames` para que el processor pueda renderizar
prefijo y sufijo en el sample exacto.

## Arquitectura lock-free

### Bancos atómicos multi-reader

Usa tres bancos de publicación. Cada campo mutable del banco es atómico y cada
banco tiene una secuencia par/impar:

```cpp
struct AtomicSnapshotBank {
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint64_t> generation{0};
    std::atomic<int> count{0};
    std::array<AtomicSection, maxSections> sections{};
};

std::array<AtomicSnapshotBank, 3> snapshotBanks{};
std::atomic<int> publishedIndex{0};
```

- El writer serializado escribe un banco no publicado, cierra su secuencia y
  publica el índice con `release`.
- Cualquier cantidad de readers puede copiar el banco publicado sin compartir
  un hazard index.
- Si un writer muy rápido vuelve a alcanzar un banco mientras un reader lo
  copia, los campos atómicos evitan data races y la secuencia descarta la copia
  mezclada.
- La adquisición realiza como máximo tres intentos y nunca toma el mutex del
  writer.

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
SectionScheduler::Snapshot currentSnapshot;
auto candidate = scheduler.acquireSnapshot();
if (candidate.valid)
    currentSnapshot = candidate;

BlockDecision decision = SectionScheduler::evaluate(
    currentSnapshot,
    currentFrame,
    blockFrames,
    useCustomArrangement,
    pendingJumpSection,
    loopSection
);

if (decision.transition) {
    renderPrefix(decision.transitionOffsetFrames);
    seekTo(decision.targetFrame);
    renderSuffix(blockFrames - decision.transitionOffsetFrames);
}
```

## Casos de uso

### Loop de sección

1. Establecer `loopSection` en evaluate
2. Cuando el bloque contiene el final, dividir en `transitionOffsetFrames`
3. Saltar a `startFrame` y renderizar el sufijo en el mismo callback

### Jump a sección

1. Establecer `pendingSection` en evaluate
2. Al alcanzar el límite actual, usar el offset exacto y saltar al destino

### Skip playback

1. Marcar `skipPlayback = true` en la sección
2. El scheduler devuelve offset `0` y el inicio de la siguiente sección

## Archivos relacionados

- [TransportClock](TransportClock.md) - Proveedor de frames
- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md) - Usa SectionScheduler
