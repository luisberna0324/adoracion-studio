---
type: Integration Contract
title: Cloud Sync — contenido cloud vs estado local
description: Contrato técnico del sync con R2 + Firestore. Define qué se
  sube a la nube, qué se queda sólo en el dispositivo, cómo se detecta un
  cambio real antes de crear una nueva revisión y cómo se reconstruye el
  proyecto local al descargar sin pisar el estado de mixer/UI.
tags: [cloud, r2, firestore, sync, mixer, pwproj, descriptor]
timestamp: 2026-09-04T12:48:00Z
---

# Cloud Sync — contenido cloud vs estado local

Esta página es el contrato explícito entre `juce-controls-plugin`
(`src/data/Cloud*`, `src/data/SongSyncService`) y el backend
`playworship` (`lib/cloud-sync.ts`, `app/api/song-sync/*`). Define qué
forma parte del contenido portable que se sube a R2 y qué se considera
estado del dispositivo que nunca debe abandonar el equipo.

Es la fuente de verdad para:

- Decidir qué objetos entran al `CloudProjectDescriptor`.
- Decidir qué objetos nunca deben filtrarse en el `manifest.json`
  portable aunque vivan dentro del `.pwproj` fuente.
- Definir el preflight de cambios antes de crear una nueva revisión.
- Reconstruir un `.pwproj` en otro dispositivo sin pisar el estado
  local de mixer/UI existente.

## Descarga local sin duplicar paquetes

Una descarga desde Cloud no crea automáticamente un `.pwproj`. Los objetos
verificados se conservan por hash en `cloud-content/objects/` y la entrada de
Biblioteca referencia directamente esos stems FLAC, artwork y waveform. Como
el motor reproduce FLAC sin generar una segunda copia en `decoded/`, cada stem
descargado ocupa una sola vez el almacenamiento local.

El `projectId` portable y el vínculo `remoteSongId`/revisión permanecen en la
Biblioteca y `cloud-sync-index.json`. `projectPath` queda vacío, por lo que la
entrada no se presenta como proyecto. Sólo la acción explícita `Convertir a
proyecto...` empaqueta esos blobs en un `.pwproj`; ese es el punto en que el
usuario solicita deliberadamente una copia portable.

La purga incluye `cloud-content/`, además de metadata, waveform, artwork e
índice cloud. Los `.pwproj` creados explícitamente siguen excluidos de la
purga.

Después de completar y registrar correctamente una descarga, la app elimina
como limpieza de compatibilidad `projects/.cloud-downloads`, usado por
versiones anteriores como staging. La eliminación es de mejor esfuerzo y no
afecta `cloud-content/objects/` ni convierte en error una descarga válida.

Para pruebas desde un dispositivo iOS físico, `localhost` no identifica el Mac
sino el propio dispositivo. El build debe definir `MTCONTROLS_API_BASE_URL` con
un hostname Bonjour o IP de la LAN, por ejemplo `http://work.local:3000`. El
plist iOS permite sólo networking local no cifrado mediante
`NSAllowsLocalNetworking` y declara `NSLocalNetworkUsageDescription`; producción
continúa usando HTTPS por defecto.

## 1. Contexto actual (auditoría 2026-09-04)

### 1.1 Formato `.pwproj` y `manifest.json`

`ProjectStore::open()` abre el ZIP y parsea `manifest.json` en
`ProjectDocument` (`src/data/ProjectStore.h:6-25`).
`settingsToVar` (`src/data/ProjectStore.cpp:378-430`) vuelca
`ProjectSongSettings` con esta forma canónica:

```jsonc
{
  "master": { "volume": 1.0, "gain": 1.0, "mute": false },
  "stems": [
    {
      "stemId": "…", "volume": 1.0, "gain": 1.0,
      "mute": false, "solo": false, "outputChannel": 1
    }
  ],
  "pitchSemitones": 0,        // ajuste por dispositivo
  "tempoBpm": 120.0,          // override por dispositivo
  "window": { "startSec": 0.0, "endSec": 0.0, "fadeOutSec": 0.0 },
  "windowCustomized": false,
  "arrangement": { "parts": [ /* SongArrangementPartSnapshot[] */ ] },
  "arrangementCustomized": false,
  "dynamicGuide": {
    "enabled": true, "timing": 0, "mode": 0,
    "languagePolicy": 0, "channelPolicy": 0
  },
  "dynamicGuideCustomized": false,
  "dynamicGuideMix": {
    "volume": 1.0, "gain": 1.0, "mute": false, "solo": false,
    "outputChannel": 1, "outputWidth": 1
  },
  "clickDynMix": {
    "divisionMask": 1, "enabled": true, "metronomeEnabled": false,
    "volume": 1.0, "gain": 1.0, "mute": false, "solo": false,
    "outputChannel": 1, "outputWidth": 1
  }
}
```

El wrapper `manifest.json` además envuelve todo con
`{ "schemaVersion": 1, "song": {…}, "settings": {…} }` (ver
`ProjectStore::open` y `settingsToVar`).

### 1.2 Cómo se sube hoy

`CloudProjectArchive::buildUpload`
(`src/data/CloudProjectArchive.cpp:59-148`) toma el `manifest.json`
íntegro extraído del ZIP, calcula su SHA-256 y lo registra como el
objeto `settings` del descriptor:

```cpp
auto settings = makeObject(manifestFile, "application/json");
request.descriptor.settings = settings.reference;
request.objects.push_back(std::move(settings));
```

El `settings` así subido contiene **todo** el bloque descrito arriba,
incluyendo master/stems/mute/solo/outputChannel, window, arrangement,
dynamic guide y mix del click — todo lo que el producto considera
estado por dispositivo.

`SongSyncService::upload`
(`src/data/SongSyncService.cpp:24-61`) delega a `CloudTransferClient`
(`src/data/CloudTransferClient.cpp:170-282`):

1. `POST /api/song-sync/prepare-upload` con hashes y tamaños.
2. `PUT` directo a las URLs firmadas de R2 (objetos + descriptor).
3. `POST /api/song-sync/commit` con metadata y hashes.

`CloudProjectArchive::rebuildProject` (`:150-217`) rehidrata el ZIP
local a partir del descriptor remoto + el `manifest.json` recibido:

- Extrae `manifest.json` del staging, valida `schemaVersion` y lo
  reinyecta **tal cual** dentro del ZIP reconstruido.
- Copia stems, waveform y artwork desde los blobs firmados.

Compatibilidad de identidad: proyectos anteriores pueden no incluir
`song.projectId` dentro del `manifest.json`; `ProjectStore::open` lo deriva de
`song.id` en memoria. Al subir se normaliza ese campo en el blob cloud y, al
descargar una revisión legacy, se completa desde `descriptor.projectId` antes
de reconstruir el ZIP. Si el manifiesto trae un ID no vacío distinto, la
descarga se rechaza.

### 1.3 Backend y descriptor remoto

`lib/cloud-sync.ts`:

- `validateCloudProjectDescriptor` (`:151-223`) exige `settings` con
  `sha256`/`sizeBytes`/`contentType`; hoy ese objeto es el
  `manifest.json` completo del `.pwproj`.
- `commitCloudSong` (`:477-563`) relee el descriptor de R2, valida
  contra los `objectHashes` enviados por el cliente y exige que
  `title/artist/bpm/key/durationSeconds` coincidan con los metadatos
  del commit. No valida el contenido de `settings` ni lo sanitiza:
  confiar en él reproduciría el bug si se mezclase.
- `createDownloadPackage` (`:565-607`) devuelve `descriptor` +
  `manifestUrl` (URL firmada del descriptor JSON en R2) + URLs de
  cada blob. **No existe hoy un endpoint "fetch remote descriptor
  sin blobs"** — el `GET /api/song-sync/songs` solo entrega un
  `CloudSongSummary` sin hashes de objetos.

### 1.4 Vínculo local ↔ remoto

`CloudSyncIndexStore`
(`src/data/CloudSyncIndexStore.cpp`) persiste en
`cloud-sync-index.json`:

```jsonc
{
  "version": 1,
  "links": [
    {
      "projectId": "…",
      "remoteSongId": "…",
      "remoteRevision": 3,
      "downloadedAtMs": 0,
      "lastSyncedAtMs": 1735920000000
    }
  ]
}
```

`LibraryCatalogPanel` (`:1184-1188`) usa `isCloudSynced` para
alternar entre `Subir a nube` y `Sincronizar de nuevo`. Esa rama
**no detecta cambios reales** — siempre dispara el flujo de subida
contra el mismo `remoteSongId`.

### 1.5 El bug concreto

`buildUpload` trata `settings` como un blob opaco y eso arrastra a R2
todo `ProjectSongSettings`. Si en cualquier momento se introduce
lógica que rehidrate `settings` desde la nube hacia
`ProjectStore::updateSettings`, el mixer/UI de un dispositivo
sobrescribirá al de otro. El log actual (2026-09-03) incluso dice
literalmente que se sube "el `manifest.json` íntegro" para "preservar
arrangement, tempo map y settings sin duplicar los serializadores
internos de `ProjectStore`" — esa decisión de diseño es la que
rompe el contrato de producto.

## 2. Contrato: contenido cloud vs estado local

### Conflictos de revisión

El backend compara `baseRevision` de la app con la revisión actual de
Firestore antes de emitir URLs de subida y vuelve a comprobarla al hacer
commit. Si no coinciden, responde `REVISION_CONFLICT` y la app no reintenta ni
actualiza la revisión local automáticamente: muestra que existe una versión
más reciente y solicita descargarla antes de volver a subir. Esto evita que
una instalación o dispositivo con un índice local antiguo sobrescriba cambios
hechos desde otro dispositivo.

### 2.1 Contenido cloud (portable, sube a R2)

Estos objetos forman parte del contenido musical compartible y
deben subirse con SHA-256 al descriptor remoto:

| Objeto | Forma | Fuente local |
|---|---|---|
| `settings` (versión sanitizada) | `CloudObjectRef` con `contentType: application/json` | `manifest.json` reescrito sólo con los campos cloud (ver §2.3) |
| `waveform` (opcional) | `CloudObjectRef` con `contentType: application/octet-stream` | Entrada `waveform.bin` del ZIP, hash estable de los bins |
| `artwork` (opcional) | `CloudObjectRef` con `contentType: image/{png,jpeg,webp}` | Archivo apuntado por `song.artworkPath` |
| `stems[]` | `CloudStemDescriptor` con `format: "flac"` | Cada `audio/<stem>.flac` del ZIP portable |

Metadata que también es cloud y debe coincidir entre descriptor y
commit (el backend ya lo exige en `commitCloudSong`):

- `title`, `artist`, `bpm`, `key`, `durationSeconds` (con la
  semántica actual de "duración del contenido musical", no del
  recorte del dispositivo).
- `createdAt`: se sella en la primera revisión y se preserva.
- `schemaVersion`: hoy `1`; un bump exige migración
  server-authoritative antes de aceptar el descriptor.

`SongArrangementSnapshot.parts` se considera contenido musical
compartible **sólo si** describe estructura musical (intro/verso/
coro/bridge, color por sección, regiones de tempo). Los campos
puramente per-dispositivo (`utilityCue`, `muteInstruments`,
`skipPlayback`) se mantienen locales. Esta parte se aborda con un
subdocumento `arrangementPortable` (ver §2.3).

### 2.2 Estado local (nunca sube a R2)

Estos bloques del `ProjectSongSettings` se quedan exclusivamente en
el sidecar `project-cache/<projectId>/song-settings.json` y nunca
forman parte de ningún blob subido:

- `mix.master.{volume, gain, mute}` y `mix.stems[]` completo
  (`volume`, `gain`, `mute`, `solo`, `outputChannel`).
- `pitchSemitones`, `tempoBpm` como override por dispositivo.
- `window.{startSec, endSec, fadeOutSec}` y `windowCustomized` (clip
  de reproducción local).
- `arrangementCustomized` (flag "el usuario lo tocó aquí"), y los
  campos per-dispositivo del arrangement (`utilityCue`,
  `muteInstruments`, `skipPlayback`).
- `dynamicGuide` (preferencias), `dynamicGuideCustomized`,
  `dynamicGuideMix`, `clickDynMix` (ruteo y mezcla del click).
- Cualquier preferencia por dispositivo, layout de UI, presets de
  routing, último setlist activo, etc. que pueda aparecer en
  futuras extensiones.

La regla operativa: si un campo cambia cuando el dispositivo cambia
de canal de salida, de interfaz de audio, de operador o de
preferencias de UI, **no va a la nube**.

### 2.3 Forma del `settings` cloud (nueva)

El objeto `settings` que se sube deja de ser el `manifest.json` del
`.pwproj` y pasa a ser un JSON portable nuevo, validado por el
backend, con esta forma:

```jsonc
{
  "schemaVersion": 2,                  // bump explícito del contrato cloud
  "song": {
    "projectId": "…",
    "title": "…", "artist": "…", "bpm": 120, "key": "C",
    "timeSignature": "4/4"
  },
  "stemsPortable": [
    { "stemId": "…", "displayName": "Drums", "category": "drums",
      "fileName": "drums.flac" }
  ],
  "artworkFileName": "artwork.png",    // si existe
  "waveformHash": "…",                 // opcional, si se decide subir
  "arrangementPortable": {             // opcional, sólo si musical
    "parts": [
      { "id": "…", "name": "Verse", "color": "…",
        "startSec": 0.0, "endSec": 32.0,
        "sourceStartSec": 0.0, "sourceEndSec": 32.0 }
    ],
    "tempoRegions": [
      { "id": "…", "startSec": 0.0, "endSec": 60.0,
        "bpm": 120.0, "timeSignature": "4/4",
        "accentEnabled": true }
    ]
  }
}
```

El backend rechazará `schemaVersion != 2`. La migración desde
`schemaVersion: 1` (con `settings = manifest.json`) requiere:

1. Una herramienta de re-upload local que reescriba el descriptor a
   `schemaVersion: 2` la primera vez que el usuario intente
   "Publicar cambios" sobre una canción legacy.
2. Mientras convivan, `commit` no debe aceptar `settings` con
   `contentType: application/json` cuyo JSON interno no valide el
   schema nuevo. Hoy no hay esa validación; hay que añadirla.

### 2.4 Sanitización del import

`CloudProjectArchive::rebuildProject` (`:150-217`) hoy reinyecta el
`manifest.json` tal cual. Hay que invertir el flujo:

1. Parsear el `settings` cloud (nuevo formato §2.3) y construir un
   `manifest.json` portable nuevo, **sin** ningún campo de
   `ProjectSongSettings` local.
2. Reconstruir el ZIP con `manifest.json` (nuevo), waveform, artwork
   y stems en `audio/<fileName>.flac`.
3. `ProjectStore::open` lo abre. `ProjectSongSettings` vuelve a sus
   defaults y los campos locales (`mix`, `window`, `dynamicGuide*`,
   `clickDynMix`, `pitchSemitones`, `tempoBpm`) se siembran desde el
   sidecar existente si hay `projectId` coincidente en
   `ProjectStore::getCachedSettings(projectId, …)`. Si no hay
   sidecar, se usan los defaults de
   `SongLibraryStore::createDefaultMixForSong` /
   `createDefaultWindowForSong` y se persiste el sidecar nuevo.
4. El resultado se registra en `SongLibraryStore::registerProjectSong`
   y en `CloudSyncIndexStore` con
   `{downloadedAtMs: now, lastSyncedAtMs: now}`.

Importante: nunca se llama a `ProjectStore::updateSettings` desde
`rebuildProject`. El sidecar local se actualiza mediante
`scheduleSettingsSave` y, si se acaban de poblar defaults, mediante
el primer guardado real del usuario.

## 3. Preflight de cambios (antes de crear revisión)

El backend ya garantiza que `commit` relee el descriptor de R2 y
exige que `title/artist/bpm/key/durationSeconds` coincidan. Falta el
paso previo: comparar el descriptor local candidato con el remoto
vigente **sin descargar blobs** y abortar si nada cambió.

### 3.1 Endpoint nuevo en el backend

`GET /api/song-sync/songs/:songId/descriptor` (auth Firebase + plan
vigente, igual que `prepare-upload`):

- Resuelve la revisión vigente desde Firestore
  (`songsCollection(uid).doc(songId)`).
- `readDescriptorFromR2({uid, songId, revision})`
  (`lib/cloud-sync.ts:417-435`).
- Devuelve `{ songId, revision, descriptor, expiresAt }` con el
  descriptor completo, **sin** URLs firmadas.

Firmar un `GET` evita exponer el descriptor sin autenticación y
permite reusar `validateCloudProjectDescriptor` para la respuesta.

### 3.2 Construcción del descriptor local candidato

`CloudProjectArchive::buildCloudDescriptor(projectFile, …)`
(nuevo, paralelo a `buildUpload`):

1. `ProjectStore::open` + `flushPendingSettings` (igual que hoy).
2. Construir el `settings` JSON nuevo (§2.3) a partir de
   `document.song` y de los campos portables del arrangement/tempo
   map. **No** leer `document.settings` para los campos locales.
3. Escribir el `settings` JSON sanitizado en staging, calcular
   SHA-256 + tamaño.
4. Extraer `waveform.bin` y el artwork como hoy.
5. Para cada stem, calcular SHA-256/tamaño y armar el
   `CloudStemDescriptor`.
6. Devolver un `CloudProjectDescriptor` con el mismo `songId`
   remoto (si hay vínculo) y `revision` que se devolverá al backend.

`buildUpload` se refactoriza para que su primer paso sea llamar a
`buildCloudDescriptor`; después sigue el flujo de subida.

### 3.3 Comparación

`CloudSyncIndexStore` ya guarda `remoteRevision`. El preflight:

1. Si no hay `link.remoteSongId`, no hay nada que comparar: la UX
   debe mostrar `Subir a nube` (camino de primera publicación).
2. Si hay vínculo, llamar a
   `GET /api/song-sync/songs/:songId/descriptor` para traer la
   última revisión server-side (sin blobs).
3. Comparar `CloudProjectDescriptor` remoto con el candidato local
   sobre estos campos:
   - `settings.sha256` y `settings.sizeBytes`.
   - `waveform?.sha256` y `waveform?.sizeBytes` (si existe).
   - `artwork?.sha256` y `artwork?.sizeBytes` (si existe).
   - Para cada `stem` (ordenado por `stemId`): `sha256`, `sizeBytes`,
     `displayName`, `category`, `durationSec`, `sampleRate`,
     `channels`, `format`.
   - `title`, `artist`, `bpm`, `key`, `durationSeconds`.
   - En `arrangementPortable.parts` (si está): `name`, `color`,
     `startSec`, `endSec`, `sourceStartSec`, `sourceEndSec`.
   - En `tempoRegions`: `startSec`, `endSec`, `bpm`,
     `timeSignature`, `accentEnabled`.
4. Si todo coincide: terminar el flujo con
   `CloudUploadResult { songId, revision = remoteRevision }` y
   resultado `PreflightNoChanges`. **No** se llama a
   `prepare-upload`, no se sube nada, no se publica revisión nueva.
5. Si difiere: continuar con `prepare-upload` + uploads +
   `commit`. La `baseRevision` que se envía es la
   `link.remoteRevision` que tenía el índice local; si en el ínterin
   otro dispositivo publicó, el backend devuelve `REVISION_CONFLICT`
   (409) y el cliente reintenta el preflight una vez antes de
   notificar al usuario.

### 3.4 Helper compartido

`CloudSyncJson` (`src/data/CloudSyncTypes.h:42-167`) ya tiene
`toVar` / `fromVar` para `CloudProjectDescriptor`. Añadir:

- `bool cloudDescriptorEquals(const CloudProjectDescriptor& a,
  const CloudProjectDescriptor& b)` que aplique la lista §3.3.
  Debe ignorar `revision` y `createdAt` (esos los fija el backend).
- `juce::String canonicalCloudDescriptorJson(
  const CloudProjectDescriptor& d)` que serialice los campos
  portables en orden estable. Útil para debug y para un log
  opcional de auditoría.

## 4. UX

`LibraryCatalogPanel` y `SongListPanel` consumen el callback
`onProjectUploadRequested` (`src/ui/panels/LibraryCatalogPanel.cpp:1396-1412`).
La rama actual decide por `isCloudSynced`, sin mirar cambios
reales. Cambios:

- Sin vínculo remoto (no aparece en `CloudSyncIndexStore`):
  `Subir a nube`. Acción primaria, siempre disponible cuando la
  sesión es válida y el proyecto tiene `.pwproj`.
- Con vínculo remoto y preflight "sin cambios": la fila muestra el
  badge `Al día` (icono check + tooltip "Última publicación
  <fecha relativa>"). La acción pasa a `Ya está al día` y queda
  deshabilitada (icono gris, no abre popover).
- Con vínculo remoto y preflight "con cambios": `Publicar cambios`.
  La barra de progreso se muestra como hasta ahora. Sin diálogo de
  éxito: el indicador de fila se actualiza a `Al día` al terminar.

Detalles que no cambian:

- `Convertir a proyecto...` sigue siendo prerrequisito para
  canciones legacy antes de cualquier acción cloud.
- `Eliminar canción` no se ve afectado.
- El `cloudLibraryClient` (catálogo `Cloud`) sigue siendo sólo
  referencia remota; no descargar ni importar desde ahí en esta
  iteración.

## 5. Cambios requeridos (resumen para implementación)

Esta página es análisis. La implementación se hace en commits
separados y con tests; el orden propuesto es:

1. Backend
   - Añadir `getCloudSongDescriptor(uid, songId)` y la ruta
     `GET /api/song-sync/songs/:songId/descriptor`.
   - Endurecer `validateCloudProjectDescriptor` para aceptar
     `schemaVersion: 2` y rechazar el `manifest.json` viejo
     (hash y/o inspección del JSON interno del objeto `settings`).
2. `juce-controls-plugin`
   - `CloudSyncTypes.h`: añadir `cloudDescriptorEquals`,
     `canonicalCloudDescriptorJson`, y constantes del nuevo
     schemaVersion portable.
   - `CloudProjectArchive`: nuevo `buildCloudDescriptor`,
     refactor de `buildUpload` para reusarlo, y `rebuildProject`
     reescrito para sembrar el sidecar local en lugar de reinyectar
     `manifest.json` (ver §2.4).
   - `CloudTransferClient`: añadir `fetchRemoteDescriptor(…,
     callback)` que use el endpoint nuevo. El callback entrega
     `CloudProjectDescriptor` (sin URLs, sin objetos).
   - `SongSyncService::upload`: implementar el preflight §3 y
     propagar `PreflightNoChanges` al callback.
   - `CloudSyncIndexStore`: añadir
     `markDownloaded(projectId, songId, revision, now)` para
     poblar `downloadedAtMs` y `lastSyncedAtMs` desde el import
     (hoy se setea en línea pero es trivial aislarlo).
   - UI (`LibraryCatalogPanel`, `SongListPanel`):
     - Sustituir el toggle `isCloudSynced` por un estado de tres
       valores: `NotLinked`, `UpToDate`, `ChangesPending`.
     - Reemplazar la etiqueta `Sincronizar de nuevo` por
       `Publicar cambios` cuando hay cambios pendientes.
     - Mostrar `Al día` (deshabilitado) cuando el preflight
       confirma que no hay cambios.
     - Mantener la barra de progreso como feedback en fila, sin
       modal de éxito.

3. Verificación
   - Recompilar el standalone contra el backend local:
     `cmake --build build-local-backend --target MTControls_Standalone -j 4`.
   - Caso manual: publicar una canción, mover un fader local,
     reabrir la app y confirmar que la nube no recibió el cambio
     de mixer.
   - Caso manual: descargar la canción en otra cuenta (o borrar
     el sidecar local) y verificar que el `.pwproj` resultante no
     sobrescribe los defaults locales al abrirse.

## 6. Compatibilidad y migración

- En Android, el puente `HttpURLConnection` habilita la salida cuando recibe
  `POSTData`, aunque los parámetros se mantengan en la dirección. Basarse sólo
  en el modo de parámetros puede anunciar `Content-Length` y enviar cero bytes.
- Canciones con `schemaVersion: 1` ya publicadas no se invalidan.
  La próxima vez que el usuario toque `Publicar cambios`, el
  cliente las reescribirá a `schemaVersion: 2` con el descriptor
  sanitizado.
- El backend debe tolerar ambos schemas durante la transición:
  `validateCloudProjectDescriptor` acepta `1` y `2`, pero sólo
  `2` permite `settings` cuyo JSON interno no sea un
  `manifest.json` completo.
- `CloudSyncIndexStore` ya tiene `version: 1`. No requiere
  migración: los campos nuevos se siembran vacíos y se llenan en
  el primer import.

## 7. Referencias

- `src/data/CloudProjectArchive.cpp:59-148` — `buildUpload`
  actual (bug).
- `src/data/CloudProjectArchive.cpp:150-217` — `rebuildProject`
  actual.
- `src/data/ProjectStore.cpp:378-430` — `settingsToVar` (todo lo
  que termina en el `settings` cloud hoy).
- `src/data/SongSyncService.cpp:24-61` — flujo de upload.
- `src/data/CloudTransferClient.cpp:170-282` — transporte + commit.
- `src/data/CloudSyncIndexStore.cpp` — vínculo local/remoto.
- `src/ui/panels/LibraryCatalogPanel.cpp:1184-1188,
  1396-1412` — UX actual `Subir a nube` / `Sincronizar de nuevo`.
- `src/PluginEditor.cpp:863-937` — handler de upload, modal de
  éxito, alert de error.
- `playworship/lib/cloud-sync.ts:151-223` —
  `validateCloudProjectDescriptor`.
- `playworship/lib/cloud-sync.ts:417-435` — `readDescriptorFromR2`
  (base del endpoint nuevo).
- `playworship/lib/cloud-sync.ts:477-563` — `commitCloudSong`
  (punto de inserción para endurecer la validación del `settings`).
- `playworship/lib/cloud-sync.ts:565-607` — `createDownloadPackage`
  (referencia para la forma del descriptor remoto).
- `playworship/types/cloud-sync.ts` — tipos compartidos.
