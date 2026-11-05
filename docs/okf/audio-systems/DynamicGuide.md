---
type: Audio System Class
title: DynamicGuide
description: Motor de guías dinámicas localizadas por idioma
resource: src/audio/DynamicGuideEngine.h
tags: [juce, audio, guide, localization, section]
timestamp: 2026-10-03T00:00:00Z
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
macOS como con Windows Media Foundation. El loader acepta WAV, AIFF, FLAC y
MP3. No usar ALAC/M4A para assets de guía: Windows no lo decodifica de forma
fiable.

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
struct Voice {
  const SectionSample *sample = nullptr;
  double position = 0.0;
  double increment = 1.0;
};
std::array<Voice, 8> voices{};
double outputSampleRate = 44100.0;
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

### Persistencia de mezcla

El volumen, gain, mute, solo y routing de la guía dinámica forman parte del
`DynamicGuideMixSnapshot` de la canción. En canciones legacy se guardan en la
biblioteca; en `.pwproj` se guardan en `ProjectSongSettings` y en su caché
local antes de actualizar el paquete. La UI también compara periódicamente el
estado publicado por el processor para persistir cambios que no provengan de
un fin de arrastre, por ejemplo controles MIDI.

Restaurar ajustes de otra canción no vuelve a decodificar el pack. Los assets
son globales para el idioma de la aplicación: `setDynamicGuideSettings()` sólo
los carga si el engine está vacío, mientras `setDynamicGuideLanguage()` los
recarga cuando cambia efectivamente el idioma. Esto mantiene la restauración de
mezcla fuera del costo de I/O y decode del pack.

La carga de una canción debe respetar siempre `DynamicGuideMixSnapshot::mute`,
incluso cuando la guía usa la configuración global por defecto
(`dynamicGuideCustomized == false`). Activar el modo de guía desde la UI puede
desmutearlo como consecuencia explícita de ese gesto; abrir o restaurar una
canción no puede hacerlo ni persistir después ese cambio como si fuera del
usuario. Este límite evita alternancias falsas en las versiones cloud de
ajustes.

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

La utilidad `Slowly Build`, mostrada como `Sube lento` en español, usa primero
su grabación exacta. Si el paquete del idioma no la incluye, el resolver cae de
forma controlada a `Build` (`Sube intensidad`) para evitar que la indicación
quede en silencio. El mismo fallback se aplica al playback y al bounce offline.

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

Al cargar una canción, la tabla de Ruteo del dispositivo se aplica después de
restaurar el snapshot de la guía. Cues gobierna tanto el stem guía reconocido por
nombre/categoría/archivo como el bus dinámico sin stem. Así una ruta estéreo
guardada en otra canción o dispositivo no invalida el Mono seleccionado en
Configuración. La herencia del stem continúa durante la reproducción.

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

## Indicaciones sobre la onda

`SongArrangementSnapshot::cues` programa instrucciones/conteos independientes
sin dividir secciones musicales. `partId+offsetSec` sigue el movimiento de su
sección; `positionSec` sirve de ubicación absoluta si no existe un padre válido.
`Breakdown` (Baja intensidad) y `A Capella` son indicaciones secundarias,
incluyendo el alias `acapella`. La migración legacy aplica las mismas
condiciones de continuidad y preservación de acciones que las demás utilidades.
Los cues son inmediatos en su punto; los anuncios de secciones musicales
conservan el adelanto de un compás. En la detección asistida, el punto de una
indicación reconocida representa su entrada musical propuesta en el compás
siguiente al aviso hablado. El tiempo original de voz permanece en el
borrador. Los conteos conservan sus instantes y no reciben ese desplazamiento.
El scheduler no resta otro compás a las indicaciones: reproduce el cue en su
posición confirmada y anticipa las principales una sola vez desde su entrada.
La detección revisa las propuestas; no modifica los eventos de un arreglo
guardado al cargarlo. El ajuste de Glorioso Día se comprobó mediante
alineación nativa desde caché, manteniendo las seis cuentas y sin cambiar el
scheduler. La evidencia valida los puntos calculados; no es una nueva escucha
humana ni un guardado del arreglo en la app. Un conteo agrupado tiene `name`
numérico,
`countEnd` (hasta 7) y offsets fuente `countOffsetsSec` relativos al primer
pulso. Esos offsets permiten atravesar cambios de BPM/métrica; el intervalo
fuente `countIntervalSec` se conserva como fallback legacy. Se expande en
puntos individuales al publicar y al exportar. Tempo convierte sus puntos fuente a los
offsets de salida de cada callback, sin cambiar el pitch de la voz.

Los nombres y aliases comunes se definen en `data/GuideVocabulary.h` y se
comparten con el editor y la detección asistida. La clasificación de utilidades
de `DynamicGuideTiming` consulta ese catálogo; los conteos conservan su ruta
numérica. Se incluyen 17 familias musicales, con Turnaround, Exhortation y Rap.
La disponibilidad de una grabación depende del pack instalado y de su idioma;
una equivalencia léxica no crea audio ni modifica los offsets de reproducción.

`DynamicGuideEvents` mantiene hasta 772 eventos callback-locales, ordenados de
forma estable por offset. En empates, primero se registran anuncios musicales
y luego cues en orden persistido. Cada trigger usa una de ocho voces fijas;
un trigger cercano no reinicia ni corta la voz anterior. Si las ocho voces
siguen activas, el nuevo trigger se rechaza: no se corta una voz ni se deja una
cola que anuncie instrucciones atrasadas. Las instrucciones demasiado próximas
pueden superponerse audiblemente; el usuario debe espaciarlas musicalmente.

Seek, loop/jump y publicación reinician el pool completo mediante el ownership
existente del callback. No existen eventos diferidos fuera de su bloque. El
bounce offline usa los mismos samples, resolución de cues y pool de voces,
renderizando entre los offsets exactos de todos los anuncios de su bloque.

Mute/volumen cero no congelan el tiempo interno de las voces: se siguen
consumiendo y el scratch se limpia antes del delay. Desmutear no puede recuperar
una instrucción antigua que debía haber terminado durante el silencio.

## Anuncios con salto de sección pendiente

Durante un Ir en cola, el lead-in existente de un compás anuncia solamente su
objetivo; no se programan anuncios de la siguiente sección secuencial ni del
loop saliente. Los counts/cues independientes conservan su calendario y el
click no cambia. Cambiar destino o cancelar y volver a programarlo rearma el
anuncio. Al cancelar, un anuncio secuencial suprimido puede emitirse en el
callback siguiente si todavía queda tiempo antes de entrar a su sección;
no se marca como oído mientras está suprimido.

Los saltos tardíos usan el lead-in inmediato existente; el anuncio no se repite
al cruzar el límite ni al entrar al destino. Una voz de destino que ya empezó
continúa completa a través del límite; se descartan las voces salientes. Si el prefijo del callback no llega
a reproducir el evento, se emite en el sufijo del destino. El timing musical de
Ir sigue siendo el final de la sección actual.

La exclusión se aplica desde el callback que observa la intención nueva.
No puede deshacer voz ya reproducida, ni muestras ya entregadas al dispositivo
(o a la compensación de latencia). Programar Ir después de oír la guía normal
puede producir esa guía ya oída seguida del destino nuevo. No se rebobina ni se
cambia el click para ocultarlo. Las voces/cues independientes que ya comenzaron
no se cortan al cambiar el destino.
