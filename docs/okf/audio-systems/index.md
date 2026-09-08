---
type: Module Index
title: Audio Systems
description: Índice de sistemas de audio
tags: [juce, audio, systems]
timestamp: 2026-07-14T20:45:00Z
---

# Audio Systems Module

Sistemas de audio auxiliary: metrónomo, pads, guías dinámicas.

## Contrato de salida standalone

El modo standalone abre una salida estéreo por defecto y conecta el
`AudioProcessorPlayer` al `AudioDeviceManager`. Sin esos dos pasos la interfaz
puede indicar reproducción, pero el callback de audio no recibe bloques y el
transporte no avanza.

En Android, el host aplica una escala global adicional de `1.12` para usar el
perfil compacto táctil en tablets de alta densidad. Este ajuste vive en
`AndroidMain.cpp` y no cambia el layout de iOS ni de escritorio.

## Sistemas

| Sistema | Descripción |
|---------|-------------|
| [Metronome](Metronome.md) | Motor de metrónomo configurable |
| [Pads](Pads.md) | Motor de pads/sampleos |
| [DynamicGuide](DynamicGuide.md) | Guías dinámicas localizadas |
| [OutputChannelCatalog](OutputChannelCatalog.md) | Catálogo de canales de salida |
| [StemOutputRouter](StemOutputRouter.md) | Enrutador de stems a canales |
| [SongMixExporter](SongMixExporter.md) | Render offline del mix actual a WAV |
| [StemCache](StemCache.md) | Cache on-disk de stems decodificados a FLAC, con migración de directorios legacy |
