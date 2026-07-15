---
type: Audio System
title: SongMixExporter
description: Exportación offline del mix de stems a WAV estéreo
resource: src/audio/SongMixExporter.h
tags: [juce, audio, export, wav, mixer]
timestamp: 2026-07-14T20:45:00Z
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
- El render se ejecuta en un hilo de fondo. El progreso se entrega mediante un
  callback y la UI debe volver al MessageManager antes de actualizar controles.
- Durante la exportación se deshabilita el botón del mixer y la waveform muestra
  la fase y el porcentaje; no debe confundirse con la carga de una canción.
- Los stems se leen desde la copia administrada por la app; si falta, se usa el
  archivo fuente original cuando todavía existe.
- El bounce usa lectura síncrona directa de los stems. No debe utilizar el
  read-ahead de reproducción en vivo: el render offline avanza más rápido que
  su worker y los misses se convierten en silencios o cortes en el archivo.
- El render incluye las fuentes virtuales activas: click dinámico y guía
  dinámica. Debe copiar sus estados de enabled, volumen, gain, mute y solo; el
  click usa BPM, métrica, división y estilo actuales, y la guía usa idioma y
  secciones del arrange. Ambas reciben la misma compensación de latencia que
  los stems procesados por tempo/pitch.
