---
type: Audio System Class
title: DynamicGuide
description: Motor de guías dinámicas localizadas por idioma
resource: src/audio/DynamicGuideEngine.h
tags: [juce, audio, guide, localization, section]
timestamp: 2026-07-25T00:00:00Z
---

# DynamicGuideEngine

Motor para reproducir guías de audio localizadas (ej: anuncios de sección en diferente idioma).

## Concepto

Las guías dinámicas son clips de audio que se reproducen en momentos específicos del arrange para anunciar la sección actual en el idioma seleccionado.

Ejemplo de uso:
- "Verse 2" en español cuando se llega a esa sección
- "Chorus" en inglés
- Anuncios de "Intro", "Bridge", "Outro"

## API Pública

### Preparación

```cpp
void prepare(double sampleRate);
void reset() noexcept;
```

Configura el motor.

### Carga de guías

```cpp
bool loadSectionDirectory(
    const juce::File &directory,
    juce::AudioFormatManager &formatManager,
    juce::String *errorMessage = nullptr
);

bool loadGuideDirectories(
    const std::vector<juce::File> &directories,
    juce::AudioFormatManager &formatManager,
    juce::String *errorMessage = nullptr
);
```

Carga guías desde directorio(s). Cada archivo representa una sección.

El pack distribuido usa MP3 para que las guías funcionen tanto con CoreAudio en
macOS como con Windows Media Foundation. No usar ALAC/M4A para assets de guía:
Windows no lo decodifica de forma fiable.

### Naming de archivos

El motor deriva el `sectionKey` del nombre del archivo:

```
verse_english.wav → sectionKey: "verse"
chorus_spanish.wav → sectionKey: "chorus"
bridge_french.wav → sectionKey: "bridge"
```

### Triggers

```cpp
bool triggerForSectionKey(const juce::String &sectionKey);
```

Activa la guía para la sección especificada.

```cpp
int resolveTriggerId(const juce::String &sectionKey) const;
```

Obtiene el ID de trigger para una sección.

```cpp
bool triggerForId(int triggerId) noexcept;
```

Activa guía por ID (más rápido para playback).

### Render

```cpp
void render(juce::AudioBuffer<float> &output) noexcept;
```

Mezcla la guía activa en el buffer de salida.

### Consulta

```cpp
int getLoadedSectionCount() const noexcept;
```

Retorna cuántas guías están cargadas.

```cpp
bool consumeLastMissingSectionKey(juce::String &outKey);
```

Retorna la última sección solicitada que no estaba disponible.

### Utilidades estáticas

```cpp
static juce::String normalizeSectionKey(const juce::String &raw);
static juce::String deriveSectionKeyFromFilename(const juce::String &fileName);
```

Normaliza y deriva section keys de strings/filenames.

## Estructura interna

### SectionSample

```cpp
struct SectionSample {
    juce::AudioBuffer<float> data;
    double sampleRate = 0.0;
};
```

Sample de audio para una sección.

### Maps internos

```cpp
std::unordered_map<std::string, std::shared_ptr<SectionSample>> samplesByKey;
// "verse" → sample
// "chorus" → sample

std::unordered_map<std::string, int> triggerIdByKey;
// "verse" → 0
// "chorus" → 1

std::vector<std::shared_ptr<SectionSample>> samplesByTriggerId;
// [0] → verse sample
// [1] → chorus sample
```

### Estado de reproducción

```cpp
const SectionSample *activeSample = nullptr;
double activePosition = 0.0;
double activeIncrement = 1.0;
double outputSampleRate = 0.0;
```

## Ejemplo de uso

### Setup

```cpp
DynamicGuideEngine guide;
guide.prepare(48000.0);

// Cargar guías en español
guide.loadGuideDirectories({
    juce::File("/guides/spanish/intro"),
    juce::File("/guides/spanish/verse"),
    juce::File("/guides/spanish/chorus")
});
```

### Durante playback

```cpp
// Cuando cambia a sección
void onSectionChange(int sectionIndex, const String &sectionKey) {
    guide.triggerForSectionKey(sectionKey);
}

// En callback de audio
void audioCallback(AudioBuffer<float> &buffer) {
    // ... otros procesamiento ...
    guide.render(buffer);  // Mezcla guía en buffer
}
```

### Verificar guías faltantes

```cpp
String missingKey;
if (guide.consumeLastMissingSectionKey(missingKey)) {
    std::cout << "Guía faltante: " << missingKey << "\n";
    // Notificar al usuario o fallback
}
```

## Integración con SectionScheduler

El `SectionScheduler` puede configurarse para trigger guías:

```cpp
SectionScheduler::Section section;
section.guideTriggerId = guide.resolveTriggerId("verse");
section.triggerGuideOnEntry = true;
```

Cuando el scheduler detecta entrada a la sección, la guía se activa.

## Integración con MTControlsAudioProcessor

```cpp
// Configurar idioma
processor.setDynamicGuideLanguage(AppLanguage::Spanish);

// Configurar volumen
processor.setDynamicGuideVolume(0.8f);

// En MIDI o arrange
processor.consumeDynamicGuideMissingSectionWarning(sectionName);
```

## Casos de uso

### Live performance

```
Intro → "Intro"
Verse 1 → "Verso"
Chorus → "Coro"
Bridge → "Puente"
Chorus → "Coro final"
Outro → "Final"
```

### Multilingüe

```
/guides/english/*.wav   → sectionKeys: "intro", "verse", "chorus"
/guides/spanish/*.wav   → same sectionKeys
/guides/french/*.wav    → same sectionKeys
```

Cambiar idioma = cambiar directorio de guías.

## Compensación de latencia

Cuando pitch o tempo introducen latencia en los stems, la salida de la guía
debe pasar por la misma compensación (`stemOutputLatencyThisBlock`). La línea
de retardo se procesa también durante bloques silenciosos para que el primer
anuncio no quede adelantado respecto de los instrumentos.

## Enrutamiento de salida (contrato con el músico)

El bus de la guía dinámica se publica por default como **mono en Out 1**, igual
que el click. El render buffer interno es siempre estéreo (2 canales) para no
perder información del sample original; cuando el routing efectivo es mono, el
`StemOutputRouter` colapsa L+R a un único canal físico promediando ambos lados.
Esto evita que la guía se monte sobre la mezcla principal cuando el host tiene
2+ canales de salida y le da al músico un bus predecible que no compite con el
resto.

Anteriormente el default era estéreo en Out 1-2. Varios usuarios reportaron
que la guía "salía por el canal 2" o se solapaba con la mezcla principal; el
problema era justamente la asimetría con el click (mono Out 1). El default
actual puede revertirse a estéreo desde la UI de routing si el músico lo
necesita.

| Default | Valor | Razón |
|---------|-------|-------|
| `dynamicGuideOutputChannelState` | `1` (Out 1) | Comparte el bus con el click |
| `dynamicGuideOutputWidthState` | `1` (mono) | No se monta sobre Out 1-2 de la mezcla |
| `DynamicGuideMixSnapshot::outputChannel` | `1` | Persiste la elección del músico |
| `DynamicGuideMixSnapshot::outputWidth` | `1` | Idem |

Cuando un stem está marcado como `stemGuideStates[i] = true`, la guía hereda
el canal y el ancho configurados para ese stem (vía
`getDynamicGuideOutputChannel` / `getDynamicGuideOutputWidth`). En ese caso
los defaults de mono Out 1 no aplican y la guía sigue al stem.

## Estado compartido con arrange

La lista de anuncios consumidos es almacenamiento fijo para
`SectionScheduler::maxSections`; publicar o editar un arrange nunca redimensiona
un contenedor que el callback esté indexando.

Los hilos de control no llaman `reset()` sobre la voz ni escriben
`dynamicGuideLastPartIndex`, `dynamicGuideWasPlaying` o la lista de anuncios.
En su lugar incrementan `dynamicGuideResetGeneration`. El callback consume esa
generación al inicio del bloque y reinicia conjuntamente voz, delay y estado
audio-thread-owned. Al recargar assets se recompila además el snapshot del
scheduler, porque los IDs enteros de trigger dependen del pack actualmente
cargado.

## Archivos relacionados

- [SectionScheduler](../../engine/SectionScheduler.md)
- [MTControlsAudioProcessor](../../processor/MTControlsAudioProcessor.md)
