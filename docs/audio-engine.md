# Motor de Audio (Adoración Estudio)

Este documento describe cómo está construido el motor de audio y cómo fluye el audio en tiempo real.

## Componentes principales

- **StemTrack**: Uno por stem. Lee/streaming del audio y aplica volumen/ganancia/mute/solo por stem.
- **MixerAudioSource**: Suma los stems cuando no se requiere mezcla separada.
- **Buffers de mezcla separada** (cuando se necesita):
  - `stemMixBuffer`: stems melódicos (a estos se aplica pitch‑shift).
  - `percussionBuffer`: stems de percusión (no se transpone).
  - `dryBuffer`: click/guía.
- **PitchShiftProcessor (SoundTouch)**: Transposición de tono sólo para stems melódicos.
- **Compensación de latencia**: retrasa percusión y guía el mismo tiempo que SoundTouch.
- **Metrónomo**: agrega el click final si está activo.
- **Master**: volumen/ganancia/mute y fade‑out global.

## Flujo con canción cargada

Cuando hay una canción cargada, el motor escoge **una de dos rutas** por bloque:

### A) Ruta de mezcla separada (cuando aplica)
Se usa si:
- Hay mono click
- Hay fade/ventana activa
- Hay pitch‑shift activo

**Flujo (texto):**

```
StemTracks
  -> se separan por categoría:
     - Click/Guía  -> dryBuffer
     - Percusión  -> percussionBuffer
     - Melódicos  -> stemMixBuffer

stemMixBuffer
  -> (si hay pitch) SoundTouch pitch‑shift
  -> aplicar ganancia de fade/ventana

percussionBuffer
  -> aplicar ganancia de fade/ventana

Si hay pitch:
  retrasar percussionBuffer + dryBuffer por la latencia de SoundTouch

Suma final:
  - si hay mono click: Left = (melodía + percusión), Right = guía
  - si no: suma melodía + percusión + guía

-> Metrónomo
-> Volumen/Ganancia/Mute master
-> Salida + medición
```

**Notas:**
- El pitch‑shift se aplica **solo** a los stems melódicos.
- Percusión y guía se **retrasan** cuando hay pitch para mantener alineación.

### B) Ruta simple (cuando NO hay pitch, mono click ni fades)
Se usa `MixerAudioSource` para eficiencia:

```
StemTracks
  -> MixerAudioSource
  -> Buffer combinado
  -> Fade/ventana
  -> Metrónomo
  -> Volumen/Ganancia/Mute master
  -> Salida + medición
```

## Flujo sin canción cargada

Cuando no hay canción cargada, el plugin funciona como FX simple:

```
Entrada de audio
  -> ¿Bypass?
     - Sí: salida directa
     - No: Input Gain -> Dry/Wet -> salida
```

## Pitch‑shift y compensación de latencia

- SoundTouch introduce latencia interna.
- Esa latencia se consulta con `SETTING_INITIAL_LATENCY`.
- Para mantener sincronía:
  - `percussionBuffer` y `dryBuffer` se retrasan exactamente lo mismo.

## Reglas de categorías (pitch‑shift)

Un stem **NO** se transpone si su categoría es:
- `Drums`
- `Percussion`
- `Click`
- `Cues`

Todo lo demás se considera melódico.

## Dónde está en el código

- **Loop principal**: `src/PluginProcessor.cpp` (`processBlock`)
- **Pitch processor**: `src/PitchShiftProcessor.h/.cpp`
- **Categorías**: `src/data/SongData.h/.cpp`
- **Control UI de pitch**: `src/ui/panels/TopBar.h/.cpp`

---

Si quieres, puedo agregar un diagrama ASCII con cajas o detallar el orden exacto de fades/solo/mute y el metrónomo paso a paso.
