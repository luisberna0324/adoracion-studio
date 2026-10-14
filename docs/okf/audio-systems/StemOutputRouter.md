---
type: Audio Routing Class
title: StemOutputRouter
description: Enrutador de stems a canales de salida
resource: src/audio/StemOutputRouter.h
tags: [juce, audio, routing, stem, output]
timestamp: 2026-07-14T20:45:00Z
---

# StemOutputRouter

Enruta el audio de un stem a los canales de salida especificados.

## Concepto

Cada stem puede ser enviado a cualquier canal de salida, con ancho configurable (mono o estéreo).

## API Pública

### Routing

```cpp
void routeStemToOutputChannels(
    const juce::AudioBuffer<float> &stemBuffer,
    int outputChannelIndex,
    int width,
    juce::AudioBuffer<float> &outputBuffer
) const;
```

Enruta el contenido de `stemBuffer` al `outputBuffer` comenzando en `outputChannelIndex`.

### Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `stemBuffer` | `AudioBuffer<float>&` | Buffer con audio del stem |
| `outputChannelIndex` | `int` | Índice del canal de salida (0-indexed) |
| `width` | `int` | 1=mono, 2=estéreo |
| `outputBuffer` | `AudioBuffer<float>&` | Buffer de salida (se modifica in-place) |

## Ejemplo

```cpp
StemOutputRouter router;
AudioBuffer<float> stemBuffer = ...;
AudioBuffer<float> outputBuffer(numChannels, blockSize);

// Stem 0 va a Out 3-4 (estéreo)
router.routeStemToOutputChannels(stemBuffer, 2, 2, outputBuffer);

// Ahora outputBuffer[2] y outputBuffer[3] tienen audio del stem
```

## Casos de uso

### Mono

```cpp
// Stem mono a canal único
router.routeStemToOutputChannels(stem, 0, 1, output);
```

### Estéreo

```cpp
// Stem estéreo a par de canales
router.routeStemToOutputChannels(stem, 0, 2, output);
```

### Sub-mix

```cpp
// Múltiples stems al mismo bus
for (int i = 0; i < stemCount; i++) {
    int channel = processor.getStemOutputChannel(i);
    int width = processor.getStemOutputWidth(i);
    router.routeStemToOutputChannels(stemBuffers[i], channel - 1, width, outputBus);
}
```

## Consideraciones

- `outputChannelIndex` es 0-indexed internamente
- La UI puede mostrar 1-indexed ("Out 1" = índice 0)
- El caller debe asegurar que hay suficiente espacio en outputBuffer

## Ruteo de canción y dispositivo

`MTControlsAudioProcessor::applySongOutputRoutingDefaults(song, defaults)` aplica
la tabla del dispositivo en el hilo de control después de restaurar la mezcla
legacy o `.pwproj`. La tabla gobierna canal y ancho; un snapshot de canción no
vuelve a sobrescribirlos después. Volumen, gain, mute y solo conservan su
restauración habitual.

- Click y Cues se aplican también a Click Dyn y Guía Dyn sin stems equivalentes.
- Los stems usan la misma identidad que el cargador (`stemPriorityForStripOrder`):
  Click/Clic y Guía/Guide/Cue reconocidos por nombre, categoría o archivo reciben
  sus respectivas rutas aunque la categoría almacenada sea Other o Keys.
- El catálogo de ruteo se limita al número de salidas activas que recibe el
  procesador, no a la capacidad total anunciada por el dispositivo. Cambiar la
  máscara de salidas dentro del mismo dispositivo también reaplica su tabla.
  Con una sola salida activa, los defaults estéreo se aplican como mono para no
  descartar stems al intentar escribir un segundo canal inexistente.
- Mono pliega L+R al destino elegido y deja sin señal de esa fuente los demás
  canales del buffer. Esta regla es común a Windows, macOS, Linux, iOS y Android.

`standalone_audio_runtime` comprueba clic y guía reales tras el callback del
player; `stem_output_router` y `stem_mix_engine` cubren el plegado y el aislamiento
con procesamiento de tempo/tono. El dispositivo simulado no verifica las
conversiones posteriores de WASAPI/ASIO ni la salida física de Windows.

## Archivos relacionados

- [OutputChannelCatalog](OutputChannelCatalog.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
