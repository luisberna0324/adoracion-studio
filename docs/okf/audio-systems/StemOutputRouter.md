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

## Archivos relacionados

- [OutputChannelCatalog](OutputChannelCatalog.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
