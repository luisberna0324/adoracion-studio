---
type: Audio System
title: SongMixExporter
description: Exportación offline del mix de stems a WAV estéreo
resource: src/audio/SongMixExporter.h
tags: [juce, audio, export, wav, mixer]
timestamp: 2026-07-25T00:00:00Z
---

# SongMixExporter

`SongMixExporter` renderiza fuera del hilo de UI un bounce estéreo de la ventana
actual de la canción. Toma el volumen, ganancia, mute y solo de cada stem, el
master, tempo, pitch y fade configurados en el procesador.

## Contratos

- La exportación no usa el dispositivo de audio ni altera el transporte en vivo.
- El usuario puede elegir WAV de 24 bits, MP3 a 320 kbps o M4A/AAC a 256 kbps.
  MP3 y M4A se codifican desde el mismo render WAV temporal mediante el encoder
  administrado por `AssetBootstrapManager`.
- En iOS y Android se ofrece WAV de 24 bits. El archivo se renderiza primero en
  el directorio temporal privado y, al completarse, se entrega a la hoja nativa
  para Guardar en Archivos, compartir o enviar. No se escribe directamente en
  la URL de un document provider. El temporal se elimina al cerrar la hoja.
  Ambos targets deben compilar `juce_gui_basics` con
  `JUCE_CONTENT_SHARING=1`; Android ademas declara
  `JuceSharingContentProvider` con la autoridad del bundle.
- MP3 y M4A permanecen disponibles en escritorio. No se ofrecen en móvil porque
  dependen del proceso externo FFmpeg y los binarios móviles no pueden ejecutar
  ese helper de escritorio.
- El render se ejecuta en un hilo de fondo. El progreso se entrega mediante un
  callback y la UI debe volver al MessageManager antes de actualizar controles.
- Durante la exportación se deshabilita el botón del mixer y la waveform muestra
  la fase y el porcentaje; no debe confundirse con la carga de una canción.
- Los stems se leen desde la copia administrada por la app; si falta, se usa el
  archivo fuente original cuando todavía existe.
- Cada reader conserva su sample rate fuente; `ResamplingPositionableSource`
  lo convierte al sample rate elegido para la salida.
- El bounce usa lectura síncrona directa de los stems. No debe utilizar el
  read-ahead de reproducción en vivo: el render offline avanza más rápido que
  su worker y los misses se convierten en silencios o cortes en el archivo.
- `startSeconds` y `endSeconds` pertenecen al timeline fuente. El ratio de
  tempo usa la convención `source/output` y se normaliza al mismo rango del
  motor (`0.75..2.0`). La cantidad de muestras escrita es
  `round(sourceFrames / tempoRatio)`.
- `song_mix_export::Timeline` distribuye los frames fuente de forma acumulativa
  entre bloques de salida. El último límite consume exactamente el rango
  seleccionado: no se trunca tempo lento ni se lee material posterior con
  tempo rápido.
- Fade y secciones de guía se evalúan en tiempo fuente. El click se sincroniza
  en tiempo de reproducción (`sourceSeconds / tempoRatio`) usando el BPM de
  playback, para conservar la misma fase musical al cambiar tempo.
- Las secciones de guía primero se convierten a su tiempo real de disparo y se
  ordenan establemente por ese valor. Esto permite intercalar una sección
  musical anunciada un compás antes con conteos o utilidades disparados al
  entrar sin omitir eventos. El lead usa el mismo dominio fuente que el
  transporte en vivo: `barSecondsAtPlaybackBpm * tempoRatio`.
- Tiempos de guía no finitos se descartan. Volúmenes y gains no finitos usan
  valores neutros antes de limitarse, para impedir que un `NaN` contamine el
  archivo.
- Si la ventana comienza exactamente sobre un beat, el click de ese límite se
  dispara antes de aplicar su compensación de latencia.
- Los disparos de guía conservan resolución de bloque (máximo 512 frames).
  Hacerlos sample-accurate requiere que `DynamicGuideEngine` pueda renderizar
  un subrango o iniciar una voz con offset dentro del bloque.
- El render incluye las fuentes virtuales activas: click dinámico y guía
  dinámica. Debe copiar sus estados de enabled, volumen, gain, mute y solo; el
  click usa BPM, métrica, división y estilo actuales, y la guía usa idioma y
  secciones del arrange. Ambas reciben la misma compensación de latencia que
  los stems procesados por tempo/pitch.
- Cuando tono o tempo activan el time-stretch, el exportador calienta las voces
  con silencio hasta estabilizar su latencia de salida. Después reposiciona los
  stems al inicio solicitado, descarta esa latencia inicial y, al terminar el
  rango fuente, alimenta silencio desde EOF para drenar la cola. El archivo
  conserva sólo el rango alineado; el preroll y el flush no aumentan su
  duración.
- La latencia publicada por `StemMixEngine::getOutputLatencySamples()` está en
  frames de salida. El exportador no debe volver a escalarla por tempo. Se
  rechaza el render si no se estabiliza o supera 65536 frames, que es la
  capacidad de sus delays virtuales. Esos delays entregan el retardo completo
  desde un ring limpio, por lo que click y guía no requieren un prellenado
  manual adicional.
- La suma `outputFrames + latencyFrames` se valida antes del loop de render para
  evitar overflow en bounces de tamaño no representable.
- El progreso de render se calcula con frames alineados efectivamente escritos,
  no con input consumido ni con preroll/flush. `Result::durationSeconds` es
  `writtenFrames / sampleRate`.
- El fade de ventana sigue el contrato de reproducción: afecta los stems
  instrumentales, mientras click y guía virtual permanecen audibles. El master
  sí se aplica al mix completo.
- Una fuente virtual sólo participa de la decisión global de solo cuando está
  habilitada; un `soloed` persistido en una fuente deshabilitada no silencia los
  stems.
