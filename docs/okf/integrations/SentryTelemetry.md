---
type: Integration Contract
title: Sentry Telemetry
description: Telemetría categórica y privada para PlayWorship, fuera del hilo realtime
resource: src/telemetry/Telemetry.h
tags: [sentry, telemetry, privacy, realtime]
---

# Sentry Telemetry

PlayWorship usa el proyecto Sentry `playworship-app`. La integración se activa
solamente en builds que reciben `SENTRY_DSN` y una copia local explícita de
`sentry-native` (`SENTRY_NATIVE_DIR`). No se descarga ni se incluye un DSN en
el código fuente.

## Contrato realtime

`processBlock` no puede llamar a una SDK, red, logger, lock ni asignar memoria.
Para gaps del callback sólo invoca `recordAudioCallbackGap()`, que hace un
`fetch_add` atómico. `timerCallback()` consume el contador y emite un evento
agregado `audio.callback_gaps` fuera del hilo de audio. `AudioDiagnostics`
sigue siendo la fuente local canónica para el diagnóstico de performance y sus
CSV no se adjuntan a Sentry.

## Privacidad

La API pública usa un enum cerrado: auth, validación de acceso, sync,
importación, carga de canción y cambio de dispositivo. No acepta mensajes,
excepciones, rutas, títulos, nombres de proyectos/canciones, URL, email ni
tokens. La app no adjunta logs locales, dumps, payloads o archivos.

## Release y símbolos

El release tiene la forma `playworship-app@<versión>+<build>.<commit>` y se
deriva de `MTCONTROLS_APP_VERSION`, `MTCONTROLS_IOS_BUILD_NUMBER` y
`GITHUB_SHA`/el commit local. Para símbolos de builds release, CI debe proveer
`SENTRY_AUTH_TOKEN`, `SENTRY_ORG=lujuma` y `SENTRY_PROJECT=playworship-app`,
y ejecutar `sentry-cli debug-files upload` sobre el bundle producido.

Entornos permitidos: `development`, `staging`, `production`.
