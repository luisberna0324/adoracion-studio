---
type: Release Contract
title: Mac App Store — perfil opcional de PlayWorship
description: Configuracion sandbox y limites de la variante Mac App Store, separada del escritorio habitual.
tags: [macos, app-store, sandbox, signing, privacy]
timestamp: 2026-10-05
---

# Mac App Store — perfil opcional de PlayWorship

`MTCONTROLS_MAC_APP_STORE=ON` crea una variante para macOS con Xcode. Por
defecto esta desactivada. El perfil de escritorio habitual conserva su
empaquetado, helpers y firma local; iOS y Android mantienen sus configuraciones.

## Bundle y firma

- Bundle ID `com.multitracks.playworship`, categoria Music y build macOS entero
  independiente mediante `MTCONTROLS_MAC_BUILD_NUMBER`.
- `MTCONTROLS_MAC_DEVELOPMENT_TEAM` debe indicar el equipo, confirmado en la
  ficha comun como `V9T23PX769`. Xcode usa firma automatica. El perfil no
  selecciona ni crea claves privadas por su cuenta.
- JUCE genera entitlements de App Sandbox: networking cliente/servidor
  (Cloud, remote y callback OAuth desktop), USB/Bluetooth (audio/MIDI) y
  lectura/escritura de archivos elegidos mediante paneles del sistema. No se
  conceden excepciones de todo el filesystem ni permiso de microfono.
- Se incluye `PrivacyInfo.xcprivacy` en `Contents/Resources`; sus declaraciones
  deben reconciliarse con el binario, los SDKs y el backend reales.
- Bungee se incorpora como framework y Xcode lo firma al copiar. CMake prepara
  una copia del framework en el build con `CFBundleIdentifier`, conservando
  el SDK original. La variante Store omite el POST_BUILD que firma ad hoc la
  app de escritorio y no transforma el framework en una dylib ad hoc.
- Release genera simbolos para dSYM. Un Archive firmado para desarrollo debe
  exportarse con firma de distribucion antes de entregarlo a la tienda.

## Archivos, importacion y exportacion

Los stems y las portadas seleccionados se copian primero a almacenamiento
privado del borrador. Al guardar, la app empaqueta el proyecto dentro de su
Biblioteca. Un `.pwproj` seleccionado para abrir tambien se copia a
`imports/projects` del contenedor. Estas copias permiten reabrir la cancion
despues de salir de la app sin conservar acceso permanente al archivo externo.

Convertir una cancion a proyecto mantiene el proyecto registrado dentro de
Biblioteca. Si el usuario elige una ubicacion externa, recibe una copia
exportada; las ediciones posteriores pertenecen a la copia de Biblioteca. Un
fallo al copiar la exportacion conserva el proyecto interno.

La conversion de stems usa FLAC con codecs integrados. Mac mantiene M4A como
entrada mediante el lector nativo de CoreAudio; no necesita descargar FFmpeg.
WAV, AIFF, FLAC y MP3 conservan sus codecs habituales. Un M4A que CoreAudio no
pueda decodificar produce error, sin aceptar un archivo ilegible ni borrar un
destino existente. Ver [Library Persistence](../importing/LibraryPersistence.md).

El perfil inicial ofrece exportacion WAV de 24 bits. MP3/M4A de salida requieren
el helper externo del escritorio y no se ofrecen en esta variante. CMake
rechaza `MTCONTROLS_BUNDLE_FFMPEG=ON`, y el gestor de assets no devuelve un
encoder ni intenta descargarlo aunque encuentre una copia de una version
anterior. Ver [SongMixExporter](../audio-systems/SongMixExporter.md).

La creacion de canciones usa el selector de audio de Biblioteca en todas las
variantes. No se consulta el almacenamiento privado de otras aplicaciones.

## Configuracion y verificacion

Ejemplo de configuracion para preparar un candidato, sin subirlo:

```bash
cmake -S . -B build/mac-appstore -G Xcode \
  -DJUCE_DIR=../third_party/JUCE \
  -DMTCONTROLS_MAC_APP_STORE=ON \
  -DMTCONTROLS_MAC_DEVELOPMENT_TEAM=V9T23PX769 \
  -DMTCONTROLS_MAC_BUILD_NUMBER=1 \
  -DMTCONTROLS_BACKEND_MODE=cloud \
  -DMTCONTROLS_ENABLE_SENTRY=OFF

xcodebuild -project build/mac-appstore/MTControls.xcodeproj \
  -scheme MTControls_Standalone -configuration Release \
  -destination 'generic/platform=macOS' \
  -archivePath build/mac-appstore/archive/PlayWorship.xcarchive \
  -allowProvisioningUpdates archive
```

Comprobar el bundle firmado del Archive con
`python3 scripts/validate_mac_app_store.py <PlayWorship.app>`; permitir una
firma de desarrollo solo al evaluar un candidato local mediante
`--allow-development-signing`. El validador comprueba firma y equipo,
entitlements, manifiesto, metadata, dependencias Mach-O incluidas y ausencia
de helpers externos. No contacta Apple ni modifica el bundle.

Las pruebas `MTControlsMacAppStoreTests`, habilitadas con
`MTCONTROLS_BUILD_TESTS=ON` en este perfil, usan almacenamiento temporal
aislado. Verifican rechazo del encoder incluso con una copia previa, ausencia
de escrituras de bootstrap, copias privadas, importacion nativa M4A a FLAC y
fallos atomicos. `afconvert` solo genera el fixture de prueba; no se incorpora
ni ejecuta en la app.

## Pendientes de aceptacion

El 5 de octubre de 2026 se genero el Archive universal 1.1.4 (1) y su paquete
de distribucion con el equipo V9T23PX769. El validador del paquete exportado
verifico firma, privacidad, sandbox y ambos Mach-O. La prueba
`mac_app_store_safety` paso; los dSYM de la app coinciden en ambas arquitecturas
y se incluyeron en el Archive. El inicio del bundle sandboxed llego al login.

Aun se requieren validacion de Apple y QA completa del bundle sandboxed: login,
importar/crear/convertir proyecto, reiniciar y reabrir Biblioteca, reproduccion,
audio/MIDI externos, remote, Cloud/Teams y exportacion WAV mediante panel Save.
Tambien probar permisos opcionales denegados.

La configuracion no resuelve por si sola login 4.8, eliminacion de cuenta,
modelo de compras y licencias, acceso del revisor ni las respuestas finales
de privacidad. El inventario de producto sigue en
[App Store](AppStore.md) y
[App Privacy](../distribution/AppPrivacyPreparation.md).

El paquete esta preparado para TestFlight; esto no acredita aprobacion ni
publicacion en la App Store. La exportacion utilizo firma automatica de Xcode
con un certificado Cloud Managed Apple Distribution y un instalador firmado.

La subida termino correctamente el 2026-10-05 a las 19:41:36 UTC. Apple
comenzo el procesamiento, confirmado tambien en la vista macOS de TestFlight
(carga `af3484c0-ed66-4c86-9038-605c9b7592a5`). No permite asignar esta build
al grupo interno hasta terminar; esa asignacion sigue pendiente.
La unica advertencia de subida fue la ausencia de
dSYM del SDK Bungee (UUIDs `3FEC9BF1-4271-30BD-A29B-D8F616757FD3` y
`6B588AD0-048C-39DA-8585-FE744AD19A34`); requiere simbolos del proveedor.
El login con Apple permanece desactivado. El log de entrega se conserva en
`/tmp/playworship-mac-store-upload.log` y el inventario de artefactos en
`output/app-store-preparation/preparation.json`.
