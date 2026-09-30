---
type: Quality Contract
title: Pruebas funcionales de PlayWorship
description: Suite automatizada, aislamiento de datos y validación en dispositivos
tags: [tests, ctest, ci, audio, mobile]
timestamp: 2026-09-26
---

# Pruebas funcionales

## Suite automatizada

El workflow `.github/workflows/tests.yml` compila el target
`PlayWorshipTests` y ejecuta CTest en cada PR. Los tests de
archivos, MIDI, creación y exportación reciben una raíz temporal nueva por proceso mediante
`tests/run_isolated_test.py`. Un retorno 77 de esos tests es un fallo:
el CI debe comprobar el flujo en lugar de informar una omisión como éxito.
En macOS, los tests de interfaz no interactivos tampoco aceptan una omisión;
en Linux sin display conservan el código de skip para ejecución local. Cada PR
también compila el target `MTControls` con los presets Windows x64 y x86, sin
empaquetar ni publicar artefactos.

El workflow `.github/workflows/android-cloudbuild.yml` compila manualmente el
APK Android arm64 en Google Cloud Build desde GitHub Actions. Usa Workload
Identity Federation para evitar guardar una llave JSON permanente en GitHub.
El APK de depuración queda disponible como artefacto del run durante 14 días.
Esta compilación no firma versiones de distribución ni reemplaza el build
firmado del workflow de releases.
Cloud Build ejecuta los pasos Linux; macOS/iOS y Windows conservan sus runners
específicos en GitHub Actions.

En macOS, con JUCE y Bungee disponibles:

```sh
cmake -S . -B build/functional-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DJUCE_DIR=/ruta/a/JUCE \
  -DMTCONTROLS_BUILD_TESTS=ON
cmake --build build/functional-tests --target PlayWorshipTests --parallel 3
ctest --test-dir build/functional-tests --output-on-failure \
  --no-tests=error -LE 'interactive|lan'
```

`input_controls_popup` requiere foco de una ventana real y lleva la etiqueta
`interactive`. Se ejecuta localmente con
`ctest --test-dir build/functional-tests -R '^input_controls_popup$'`.

En el primer run del PR, el runner alojado de macOS no entregó los paquetes
multicast mDNS del backend portátil a procesos de la misma máquina. Las pruebas
`remote_session_portable` y `remote_discovery_interop` llevan la etiqueta `lan`
y se ejecutan en una Mac con red local mediante `ctest --test-dir
build/functional-tests --output-on-failure -L lan`. El gate de PR ejecuta
`remote_session` con Bonjour nativo y `remote_session_portable_transport` con
conexión WebSocket directa a loopback; este último ejercita comandos, snapshots,
onda, VU y reconexión con la compilación portátil.

## Cobertura por contrato

| Contrato | Tests de regresión |
| --- | --- |
| [Transporte](transport/Transport.md) y [arrange](engine/SectionScheduler.md) | `audio_engine_core`, `song_mix_export_timeline` |
| [Motor de audio](engine/StemMixEngine.md), click y pads | `stem_mix_engine`, `stem_buffering_source`, `metronome_engine`, `pads_engine`, `bungee_processor` |
| [MIDI](midi/MidiBindings.md) | `midi_settings` comprueba persistencia, modos, límites y disparos de CC/Note |
| [Creación de canciones](importing/SongCreation.md) | `song_project_creation`, `song_import_wizard`, `portable_flac_conversion`, `mobile_import_staging` |
| [Configuración móvil](ui/MobileLayout.md) | `settings_panel`: navegación agrupada, retorno con scroll conservado, controles remotos entre páginas, acciones de cuenta y alcance del último selector de ruteo |
| [Ensayo por grupos](ui/Rehearsal.md) | `rehearsal`: grupos, IDs obsoletos, restauración, reconexión y rampas; `stem_mix_engine`: factor temporal en rutas procesadas y salidas dedicadas |
| [Cloud Sync](integrations/CloudSync.md) y versiones de ajustes | `cloud_settings_sync_state`, `cloud_sync_index`, `settings_versions_sheet` |
| [Exportación](audio-systems/SongMixExporter.md) | `song_mix_export_timeline`, `song_mix_exporter` |
| [Control remoto LAN](integrations/RemoteControl.md) | `remote_session`, `remote_session_portable_transport` en CI; `remote_session_portable` y `remote_discovery_interop` en una LAN |

Las pruebas de red comprueban descubrimiento, conexión, comandos, confirmaciones,
estado y reconexión entre procesos locales. Las de creación generan un `.pwproj`,
lo registran en Biblioteca y lo reabren desde esa ruta. El test de exportación
genera un WAV real y vuelve a decodificarlo para comprobar duración y fade.

## Validación en dispositivos físicos antes de publicar

Estas comprobaciones requieren hardware y se registran con dispositivo, versión
de build y resultado. La compilación o un simulador no las acreditan.

1. En iPhone y Android físicos, importar audio desde el selector nativo,
   recorrer los cuatro pasos, guardar y observar progreso. Confirmar que la
   canción aparece en Biblioteca, cerrar y abrir la app, y reabrir el `.pwproj`
   sin depender de los archivos originales. Repetir una cancelación y un error
   de archivo para comprobar que el borrador permanece editable.
2. Con un servidor en la misma LAN, abrir un cliente físico, conceder permiso
   de red local y verificar descubrimiento, conexión y primer snapshot. Probar
   play/stop, seek y volumen desde el cliente; comprobar posición, VU y mezcla
   en ambos dispositivos. Desconectar la red, verificar posición congelada y VU
   en cero, reconectar y comprobar recuperación. Volver a Local y confirmar que
   la canción y mezcla locales siguen intactas.
3. Con la salida de audio física, reproducir stems, click y pads, mover faders,
   cambiar tempo y tono y escuchar que no aparecen cortes ni audio desfasado.
   Exportar la ventana actual y escuchar el inicio y final del archivo generado.

Registrar fallos de estas comprobaciones como regresiones reproducibles y añadir
un test automatizado del contrato afectado cuando sea posible.

## Resultado local del 26/09/2026

- macOS arm64, build Debug: `PlayWorshipTests` compiló y CTest pasó 25/25 con
  `-LE interactive`. La prueba de creación abrió el `.pwproj` registrado; la
  prueba de exportación reabrió y decodificó el WAV generado.
- Los 22 tests sin red pasaron dentro del sandbox. Los tres de control remoto
  (`remote_session`, `remote_session_portable`, `remote_discovery_interop`)
  pasaron con acceso a sockets locales. El sandbox impedía iniciar el servidor
  WebSocket, así que ese primer fallo no era una regresión de protocolo.
- `input_controls_popup` quedó fuera del gate automático porque requiere foco
  interactivo. Las comprobaciones en dispositivos físicos aún no tienen un
  resultado de ejecución registrado aquí.
- El primer run de PR (#41) compiló correctamente y pasó 23/25 pruebas. Fallaron
  las dos pruebas de descubrimiento mDNS portátil en el runner alojado de macOS;
  se conservaron con etiqueta `lan` y se añadió cobertura portátil de WebSocket
  directa al gate. La configuración revisada pasó 24/24 pruebas del gate en
  macOS local, y las dos pruebas `lan` pasaron por separado (26/26 en total).
- El segundo run del PR #41 pasó 24/24 pruebas del gate en el runner macOS y
  también pasó la política SemVer. Las comprobaciones físicas siguen pendientes.
