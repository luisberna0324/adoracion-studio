---
type: Integration Contract
title: LyricsControllers
description: Contrato técnico para disparar comandos de letras desde secciones del arrange
tags: [lyrics, propresenter, easyworship, worshiptools, control, cues]
timestamp: 2026-07-18T00:00:00Z
---

# Lyrics Controllers

Este documento define el contrato técnico para integrar PlayWorship con software externo de letras y proyección.

## Objetivo

Permitir que una sección del arrange dispare una acción externa sobre una aplicación de letras cuando:
- se entra a una sección,
- se avanza a una parte del coro/verso,
- se ejecuta un jump manual,
- o se dispara un cue específico asociado a la sección.

## Alcance inicial

### Prioridad 1: ProPresenter

ProPresenter es el primer destino objetivo porque ofrece una API documentada y control de red oficial.

Fuentes oficiales:
- [ProPresenter API](https://openapi.propresenter.com/)
- [TCP/IP Connections with ProPresenter API](https://support.renewedvision.com/hc/en-us/articles/31606866768147-TCP-IP-Connections-with-ProPresenter-API)
- [Connecting to ProPresenter Control](https://support.renewedvision.com/hc/en-us/articles/6024791423763-Connecting-to-ProPresenter-Control)
- [How to Connect the ProPresenter Remote App](https://support.renewedvision.com/hc/en-us/articles/42892551789843-How-to-Connect-the-ProPresenter-Remote-App)

### Prioridad 2: EasyWorship

EasyWorship tiene control remoto oficial y pairing por red, pero no se encontró una API pública equivalente a la de ProPresenter.

Fuentes oficiales:
- [General Remote App Troubleshooting](https://support.easyworship.com/support/solutions/articles/24000031178-general-remote-app-troubleshooting)
- [Remote App And Android Phone Setup](https://support.easyworship.com/support/solutions/articles/24000039297-remote-app-and-android-phone-setup)
- [Stream Deck Plug-in](https://support.easyworship.com/support/solutions/articles/24000072291-stream-deck-plug-in)

### Prioridad 3: WorshipTools Presenter

WorshipTools tiene app remota y soporte MIDI, pero no se encontró en esta revisión una API pública documentada para control directo.

Fuentes:
- [Docs](https://www.worshiptools.com/en-us/docs)
- [Remote Mobile App](https://www.worshiptools.com/en-us/docs/202-remote-app)

## Requisitos funcionales

La integración debe soportar, como mínimo:
- disparo de siguiente / anterior slide,
- salto a una parte definida,
- disparo de macro o cue por nombre,
- envío de mensaje de pantalla o stage display,
- limpieza de pantalla o black out, si la app destino lo soporta.

## Modelo de datos propuesto

Cada sección del arrange puede asociar una acción externa:

```cpp
enum class LyricsTargetKind {
    none = 0,
    propresenter = 1,
    easyworship = 2,
    worshiptoolsPresenter = 3
};

enum class LyricsCueActionKind {
    none = 0,
    nextSlide = 1,
    previousSlide = 2,
    jumpToSection = 3,
    triggerMacro = 4,
    showMessage = 5,
    clear = 6,
    black = 7
};

struct LyricsCueAction {
    LyricsTargetKind target = LyricsTargetKind::none;
    LyricsCueActionKind action = LyricsCueActionKind::none;
    juce::String payload;
    bool fireOnEntry = true;
};
```

## Campos necesarios para conexión

### ProPresenter

- `host` o `ip address`
- `port`
- `password`
- `mode` de conexión:
  - `control`
  - `remote`
  - `tcp`
- identificador opcional de:
  - macro,
  - slide,
  - playlist,
  - mensaje stage display

### EasyWorship

- `host` o descubrimiento local
- estado del Remote App
- credenciales o pairing si aplica
- tipo de acción permitido por la versión instalada

### WorshipTools Presenter

- identidad de cuenta / sesión,
- estado de Remote,
- tipo de acción disponible por la versión instalada.

## Flujo runtime

1. `SectionScheduler` determina la sección activa.
2. `MTControlsAudioProcessor` o la capa de control notifica el cambio de sección.
3. Un controlador externo traduce la acción configurada a una solicitud de red o comando local.
4. La app de letras actualiza la slide o el cue correspondiente.

## Restricciones técnicas

- No ejecutar esta lógica en el audio thread.
- Las acciones deben salir por una capa de control asíncrona.
- Si la app destino no responde, la reproducción de audio no debe bloquearse.
- La integración debe ser opcional por canción o por sección.

## Estrategia de implementación

1. Crear una abstracción única de destino y acción.
2. Implementar primero un adapter para ProPresenter.
3. Añadir soporte para EasyWorship como integración específica según sus capacidades reales.
4. Evaluar WorshipTools Presenter si se confirma un canal de control estable.

## Archivos relacionados

- [SectionScheduler](../engine/SectionScheduler.md)
- [Transport](../transport/Transport.md)
- [MTControlsAudioProcessor](../processor/MTControlsAudioProcessor.md)
