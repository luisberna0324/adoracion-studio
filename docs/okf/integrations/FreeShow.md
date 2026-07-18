---
type: Integration Contract
title: FreeShow
description: Contrato tecnico y workflow para integrar PlayWorship con FreeShow
tags: [freeshow, lyrics, presenter, websocket, rest, remote, cues]
timestamp: 2026-07-18T00:00:00Z
---

# FreeShow Integration

FreeShow es un presentador open source con API documentada, control remoto por red y acciones reutilizables en slides, MIDI y funciones internas.

Fuentes oficiales:
- [API Docs](https://freeshow.app/api)
- [Functions](https://freeshow.app/docs/functions)
- [Remote](https://freeshow.app/docs/remote)
- [Connect remotely](https://freeshow.app/docs/connecting)
- [FAQ](https://freeshow.app/docs/faq)

## Capacidades relevantes de la API

### Control de proyectos y shows

La API permite seleccionar proyectos, shows y elementos dentro del proyecto.

Acciones relevantes:
- seleccionar proyecto por id, índice o nombre
- avanzar o retroceder item del proyecto
- seleccionar show por nombre
- iniciar un show
- cambiar layout
- rearranjar grupos
- cambiar template
- cambiar texto simple

### Edición y contenido

La API también expone acciones para crear y modificar contenido:
- crear proyecto
- crear show
- agregar o remover items
- renombrar proyecto
- establecer texto plano

### Consultas de estado

Permite leer el estado actual:
- lista de shows
- lista de proyectos
- grupos
- output actual
- slide activa
- thumbnails
- texto de output
- timers
- variables
- playlists
- estado de reproducción de audio o video
- si el contenido fue limpiado

### Acciones generales

La documentación oficial indica que las acciones se pueden disparar:
- por MIDI,
- al activar un slide,
- y vía API.

También existen acciones para ejecutar funciones nombradas y alternar estado de una acción.

### Transporte de conexión

La API puede activarse como:
- HTTP
- REST por POST
- WebSocket

La documentación oficial muestra puertos separados de ejemplo para HTTP/REST y WebSocket, y aclara que la API debe estar activa desde la sección de conexiones.

## Flujo de conexión

### Requisitos mínimos

- FreeShow abierto en la máquina destino
- API/WebSocket/REST habilitado en settings
- red local compartida
- IP y puerto accesibles

### Descubrimiento / conexión

1. Activar el servidor en `Connection`.
2. Obtener IP y puerto.
3. Conectar por:
   - HTTP directo
   - REST POST
   - WebSocket
4. Si se usa Remote, también puede usarse contraseña del propio remote.

### Datos a guardar en PlayWorship

- `host`
- `port`
- `transport`:
  - `http`
  - `rest`
  - `websocket`
  - `remote`
- `password` si se usa Remote
- `actionId`
- `payload`
- `targetShowId` o `targetProjectId`

## Mapeo de capacidades hacia PlayWorship

### 1. Sync de sección

Cuando PlayWorship entra a una sección:
- buscar una acción asociada a esa sección
- ejecutar el action id de FreeShow
- opcionalmente pasar payload con id/nombre del show o slide

### 2. Navegación de show

Mapeos directos recomendados:
- `next section` -> `next_project_item`
- `previous section` -> `previous_project_item`
- `jump to song/show` -> `id_select_project`, `name_select_show` o `start_show`
- `jump to specific slide` -> `get_show` + selección de slide o acción equivalente configurada por show

### 3. Letras y coro

Para control de letras en vivo, el mejor patrón es:
- cada sección de PlayWorship guarda un `lyrics action`
- al entrar al bloque se dispara `run_action`, `name_run_action` o `emit_action`
- FreeShow resuelve internamente qué slide, group o layout activar

### 4. Texto y overlays

FreeShow soporta:
- variables
- timers
- triggers por HTTP
- texto plano

Esto permite:
- mandar un nombre de coro o verso como texto,
- disparar overlays,
- sincronizar textos auxiliares de stage display.

### 5. Estado y monitoreo

PlayWorship puede consultar:
- output activo
- slide actual
- texto actual de output
- duración y tiempo restante de audio/video
- variables
- timers

Eso abre la puerta a una vista de estado del target en la UI del plugin.

## Workflow recomendado

### Workflow A: control de sección simple

1. El usuario define una sección en PlayWorship.
2. Asocia `FreeShow action`.
3. Al entrar a la sección, PlayWorship llama a la API.
4. FreeShow cambia el show o avanza la slide.

### Workflow B: show-driven

1. El usuario selecciona un show principal en FreeShow.
2. PlayWorship sincroniza intro, verso, coro y puente con acciones.
3. El plugin sólo dispara cues, y FreeShow mantiene su propia navegación interna.

### Workflow C: bidireccional ligero

1. PlayWorship envía comandos a FreeShow.
2. PlayWorship consulta estado actual para mostrar feedback.
3. La UI refleja slide, show y texto activo.

### Workflow D: autorun por canción

1. Una canción de PlayWorship tiene un mapping fijo a un show de FreeShow.
2. Al cargar la canción, el plugin puede seleccionar el show.
3. Al iniciar reproducción, dispara el primer cue.
4. Cada cambio de sección dispara la acción siguiente.

## Restricciones técnicas

- El audio thread no debe hacer ninguna llamada de red.
- Las acciones deben ir por un worker o dispatcher de control.
- Las respuestas de estado deben tratarse como opcionales.
- Si la API no responde, la reproducción debe seguir normal.

## Prioridad de implementación

1. Conector de conexión y health-check.
2. Selección de proyecto/show.
3. Disparo de acciones por sección.
4. Lectura de estado y feedback UI.
5. Soporte de variables, timers y triggers.
6. Integración bidireccional avanzada.

## Archivos relacionados

- [LyricsControllers](LyricsControllers.md)
- [SectionScheduler](../engine/SectionScheduler.md)
- [Transport](../transport/Transport.md)
