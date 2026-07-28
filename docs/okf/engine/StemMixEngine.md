---
type: Audio Engine Class
title: StemMixEngine
description: Motor de mezcla de stems con procesamiento de pitch y tempo
resource: src/StemMixEngine.h
tags: [juce, audio, engine, mix, stem, pitch, tempo]
timestamp: 2026-07-25T22:12:14Z
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

Cada `TimeStretchProcessor` contenido expone además:

```cpp
void releaseResources() noexcept;
```

Es una operación exclusivamente no-RT: el callback y el servicio de
mantenimiento deben estar detenidos antes de invocarla. Destruye las dos voces
Bungee (o el backend SoundTouch), libera la capacidad de sus buffers temporales
y FIFO, y borra sample rate, canales, latencia, planificador fraccional,
generaciones y diagnósticos de ejecución. El procesador queda no preparado y
no puede reconstruirse mediante el servicio hasta recibir otro `prepare()`.

Pitch, tempo y tipo de contenido son configuración lógica y se conservan; un
`prepare()` posterior reconstruye sólo los recursos solicitados aplicando esa
misma configuración. La operación es idempotente, pero puede liberar memoria y
nunca debe ejecutarse desde el hilo de audio.

### Reset

```cpp
void reset();
int servicePendingProcessorRebuilds() noexcept;
uint64_t getTimeProcessorResetMissCount() const noexcept;
uint64_t getTimeProcessorServiceFailureCount() const noexcept;
```

`reset()` limpia buffers y cambia cada Bungee activo por una segunda voz ya
construida. El cambio es lock-free y no reserva memoria. La voz anterior queda
retirada; `servicePendingProcessorRebuilds()` debe ejecutarse fuera del callback
para reconstruirla y volver a publicar un spare limpio.

Cada voz se precalienta con silencio fuera del callback hasta publicar una
latencia estable. Si llega otra discontinuidad antes de que exista un spare, el
reset queda pendiente, `getTimeProcessorResetMissCount()` aumenta y el
procesador entrega silencio en vez de reutilizar historia vieja. El worker
reconstruye el spare con backoff exponencial si falla; esos fallos se consultan
con `getTimeProcessorServiceFailureCount()`.

Antes de renderizar, el motor intenta activar todos los reemplazos requeridos
sin cortar el preflight al primer fallo. Si alguno todavía no está listo, el
bus agrupado completo se entrega en silencio y `wasLastRenderBlocked()` lo
indica al procesador principal. Las fuentes sí avanzan: la recuperación produce
un hueco coherente, nunca melodía ausente mientras percusión o guía continúan.

### Pitch shift

```cpp
void setPitchSemiTones(int semitones);
```

Establece el pitch shift en semitonos (-12 a +12).

El valor público continúa siendo entero y representa el target. En Bungee, el
valor aplicado se interpola en el dominio de semitonos durante 50 ms del
timeline de salida; así una subida o bajada no cambia la escala de frecuencia
de golpe entre callbacks. Mientras la rampa está activa, cada bloque se divide
en segmentos de aproximadamente 128 frames, con un máximo fijo de ocho llamadas
al backend. Las particiones acumulativas suman exactamente los frames de entrada
y salida originales y el callback no reserva memoria ni toma locks.

Cada segmento usa la escala media de su tramo de rampa. Al terminar los 50 ms,
la escala queda exactamente en `2^(target/12)`. `prepare()` y `reset()` arrancan
la voz nueva directamente en el target y cancelan cualquier rampa anterior;
esto hace determinista el estado después de crear o intercambiar una voz.

El fallback SoundTouch conserva su transición existente entre señal seca y
procesada; no divide el bloque ni reconstruye el backend desde el callback.

### Tempo ratio

```cpp
void setTempoRatio(double ratio);
```

Establece la proporción de tempo (1.0 = normal, 0.75 = mínimo, 2.0 = doble).

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

Renderiza a buffer de salida con fade window. El grupo `dryBuffer`, que contiene
los stems marcados como click/guía, no recibe el fade de instrumentos. Esta
misma excepción debe conservarse en el camino de routing multicanal del
procesador.

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

Retorna la latencia total en samples del dominio de salida. Los backends que
reportan su latencia en frames de entrada, como Bungee, la convierten antes de
publicarla:

```text
outputLatencySamples = inputLatencyFrames / tempoRatio
```

`tempoRatio` expresa frames de entrada consumidos por cada frame de salida.

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

Las tres delay lines se procesan en todos los bloques, incluso cuando su delay
actual es cero. Esto conserva historia suficiente para activar pitch sin
introducir un bloque de silencio al establecer la compensación. En modo
pitch-only, percusión y guía atraviesan explícitamente sus delay lines para
alinearse con el grupo melódico procesado por Bungee.

Después de `prepare()` o `reset()`, un delay mayor que cero se aplica completo
desde el primer sample: las posiciones del ring todavía no escritas producen
cero hasta acumular exactamente `delaySamples`. El modo delay cero no lee el
ring, pero sí escribe cada sample; por eso un cambio en vivo a delay positivo
puede recuperar historia real ya renderizada, sin reducir artificialmente el
retardo durante el arranque.

La validez de esa historia se lleva con un contador separado. `reset()` e
`invalidateHistory()` son O(1): no limpian el ring completo dentro del callback,
sino que tratan sus muestras anteriores como inválidas hasta escribir el delay
completo de la nueva línea temporal.

### Discontinuidades

`MTControlsAudioProcessor` atiende los tres grupos y los procesadores de
routing desde un único `TimeSliceThread`: no crea un hilo por stem. Las dos
voces Bungee evitan reutilizar granos o cola de la posición anterior después de
seek, loop o cambio estructural.

El servicio preconstruye y precalienta estado DSP limpio con silencio, pero
todavía no prealimenta audio del destino. El salto queda libre de historia
vieja y conoce su latencia desde el primer bloque; no se declara gapless hasta
disponer también de una segunda caché fuente prellenada.

## Archivos relacionados

- [StemTrack](StemTrack.md) - Track individual de audio
- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md) - Usa StemMixEngine
