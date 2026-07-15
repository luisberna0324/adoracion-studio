---
type: Utility Class
title: OutputChannelCatalog
description: Utilidad para construir nombres de canales de salida
resource: src/audio/OutputChannelCatalog.h
tags: [juce, audio, routing, channels]
timestamp: 2026-07-14T20:45:00Z
---

# OutputChannelCatalog

Utilidad estática para generar nombres de canales de salida.

## API Pública

### Canales individuales

```cpp
static juce::StringArray buildOutputChannelNames(int outputChannelCount);
```

Genera nombres para cada canal individual.

**Ejemplo:**
```cpp
// outputChannelCount = 8
// Resultado: {"Out 1", "Out 2", "Out 3", "Out 4", "Out 5", "Out 6", "Out 7", "Out 8"}
```

### Pares estéreo

```cpp
static juce::StringArray buildStereoPairNames(int outputChannelCount);
```

Genera nombres para pares estéreo.

**Ejemplo:**
```cpp
// outputChannelCount = 8
// Resultado: {"Out 1-2", "Out 3-4", "Out 5-6", "Out 7-8"}
```

## Uso con MTControlsAudioProcessor

```cpp
// Obtener nombres disponibles
auto names = processor.getAvailableOutputChannelNames();
// {"Out 1", "Out 2", "Out 3", ...}

// Obtener cantidad
int count = processor.getAvailableOutputChannelCount();

// Asignar stem a canal
processor.setStemOutputChannel(0, 3);  // Stem 0 → Out 3
processor.setStemOutputWidth(0, 2);   // Stereo (3-4)
```

## Convenciones de nombrado

| Tipo | Formato | Ejemplo |
|------|---------|---------|
| Mono | "Out N" | "Out 1" |
| Stereo | "Out N-M" | "Out 1-2" |

## Archivos relacionados

- [StemOutputRouter](StemOutputRouter.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
