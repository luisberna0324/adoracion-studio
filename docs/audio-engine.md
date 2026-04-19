# Motor de Audio (PlayWorship)

Este documento describe como esta construido el motor de audio y como fluye el audio en tiempo real.

## Componentes principales

- **StemTrack**: Uno por stem. Lee/streaming del audio y aplica volumen/ganancia/mute/solo por stem.
- **MixerAudioSource**: Suma los stems cuando no se requiere mezcla separada.
- **Buffers de mezcla separada** (cuando se necesita):
  - `stemMixBuffer`: stems melodicos (a estos se aplica pitch‑shift).
  - `percussionBuffer`: stems de percusion (no se transpone).
  - `dryBuffer`: click/guia.
- **PitchShiftProcessor (Rubber Band / SoundTouch)**: Transposicion de tono y cambio de tiempo para stems melodicos.
- **Compensacion de latencia**: retrasa percusion y guia el mismo tiempo que SoundTouch.
- **Metronomo**: agrega el click final si esta activo.
- **Master**: volumen/ganancia/mute y fade‑out global.

## Flujo con cancion cargada

Cuando hay una cancion cargada, el motor escoge **una de dos rutas** por bloque:

### A) Ruta de mezcla separada (cuando aplica)
Se usa si:
- Hay mono click
- Hay fade/ventana activa
- Hay pitch‑shift activo

**Flujo (texto):**

```
StemTracks
  -> se separan por categoria:
     - Click/Guia  -> dryBuffer
     - Percusion  -> percussionBuffer
     - Melodicos  -> stemMixBuffer

stemMixBuffer
  -> (si hay pitch) SoundTouch pitch‑shift
  -> aplicar ganancia de fade/ventana

percussionBuffer
  -> aplicar ganancia de fade/ventana

Si hay pitch:
  retrasar percussionBuffer + dryBuffer por la latencia de SoundTouch

Suma final:
  - si hay mono click: Left = (melodia + percusion), Right = guia
  - si no: suma melodia + percusion + guia

-> Metronomo
-> Volumen/Ganancia/Mute master
-> Salida + medicion
```

**Notas:**
- El pitch‑shift se aplica **solo** a los stems melodicos.
- Percusion y guia se **retrasan** cuando hay pitch para mantener alineacion.

### B) Ruta simple (cuando NO hay pitch, mono click ni fades)
Se usa `MixerAudioSource` para eficiencia:

```
StemTracks
  -> MixerAudioSource
  -> Buffer combinado
  -> Fade/ventana
  -> Metronomo
  -> Volumen/Ganancia/Mute master
  -> Salida + medicion
```

## Flujo sin cancion cargada

Cuando no hay cancion cargada, el plugin funciona como FX simple:

```
Entrada de audio
  -> ?Bypass?
     - Si: salida directa
     - No: Input Gain -> Dry/Wet -> salida
```

## Pitch‑shift y compensacion de latencia

- El backend de time/pitch introduce latencia interna (Rubber Band o SoundTouch).
- Esa latencia se consulta desde el backend activo.
- Para mantener sincronia:
  - `percussionBuffer` y `dryBuffer` se retrasan exactamente lo mismo.

## Reglas de categorias (pitch‑shift)

Un stem **NO** se transpone si su categoria es:
- `Drums`
- `Percussion`
- `Click`
- `Cues`

Todo lo demas se considera melodico.

## Donde esta en el codigo

- **Loop principal**: `src/PluginProcessor.cpp` (`processBlock`)
- **Pitch processor**: `src/PitchShiftProcessor.h/.cpp`
- **Categorias**: `src/data/SongData.h/.cpp`
- **Control UI de pitch**: `src/ui/panels/TopBar.h/.cpp`

---

Si quieres, puedo agregar un diagrama ASCII con cajas o detallar el orden exacto de fades/solo/mute y el metronomo paso a paso.
