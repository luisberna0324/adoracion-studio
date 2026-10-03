---
type: Quality Contract
title: Pruebas funcionales de PlayWorship
description: Suite automatizada, aislamiento de datos y validación en dispositivos
tags: [tests, ctest, ci, audio, mobile]
timestamp: 2026-10-03
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
`input_controls` verifica también el anclaje, la opción seleccionada/visible y
las métricas del popup tanto en `ComboBox` estándar (tema global) como en
`AdaptiveComboBox`. La prueba interactiva reutiliza el mismo contrato de
selección única, teclado, Escape y estado deshabilitado para ambos y para
el modo de formulario móvil.

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
| [Transporte](transport/Transport.md) y [arrange](engine/SectionScheduler.md) | `audio_engine_core`, `song_mix_export_timeline`, `waveform_editing` |
| [Automatización por sección](engine/SectionAutomation.md) e [interfaz](ui/SectionAutomation.md) | `section_automation`: publicación coherente, valores dispersos, base y overrides temporales, límites dentro del callback y rampas; `waveform_editing`: barra, borrador fijado y historial; `song_project_creation`: codec, paquete, sidecar y Cloud; `remote_session`: propiedad y reconciliación |
| [Motor de audio](engine/StemMixEngine.md), click y pads | `stem_mix_engine`, `stem_buffering_source`, `metronome_engine`, `pads_engine`, `bungee_processor` |
| [MIDI](midi/MidiBindings.md) | `midi_settings` comprueba persistencia, modos, límites y disparos de CC/Note |
| [Creación de canciones](importing/SongCreation.md) | `song_project_creation`, `song_import_wizard`, `portable_flac_conversion`, `mobile_import_staging` |
| [Configuración móvil](ui/MobileLayout.md) | `settings_panel`: navegación agrupada, retorno con scroll conservado, controles remotos entre páginas, acciones de cuenta y alcance del último selector de ruteo |
| [Guías interactivas](ui/LearningGuide.md) | `learning_guide`: cinco cursos, condiciones remotas, requisitos temporales de sesión al reanudar, IDs, persistencia, reinicio y entradas inválidas; `learning_overlay`: spotlight, interacción y scroll, botones compactos, bloqueo de avance por botón/teclado, reparenting sobre Settings real y targets de Ensayo; `settings_panel`: acceso, destinos y scroll de aprendizaje; `remote_learning_action`: comando propio, distinción Play/Pausa, ACK y snapshot posterior, respuestas ajenas/atrasadas, rechazo, timeout y limpieza de evidencia |
| [Ensayo por instrumento](ui/MobileLayout.md) | `practice_panel_gestures`: gestos en toda la tarjeta, enfoque de tres estados, arrastre e inercia, zoom, tono y tempo, regla y snap de A/B, limpieza de loops, geometría de onda y reloj visual; `stem_mix_engine`: ganancias temporales y transiciones de audio; `remote_session`: propiedad, comandos pendientes y reconciliación |
| [Cloud Sync](integrations/CloudSync.md) y versiones de ajustes | `cloud_settings_sync_state`, `cloud_sync_index`, `settings_versions_sheet` |
| [Exportación](audio-systems/SongMixExporter.md) | `song_mix_export_timeline`, `song_mix_exporter` |
| [Control remoto LAN](integrations/RemoteControl.md) | `remote_session`, `remote_session_portable_transport` en CI; `remote_session_portable` y `remote_discovery_interop` en una LAN |

Las pruebas de red comprueban descubrimiento, conexión, comandos, confirmaciones,
estado y reconexión entre procesos locales. Las de creación generan un `.pwproj`,
lo registran en Biblioteca y lo reabren desde esa ruta. El test de exportación
genera un WAV real y vuelve a decodificarlo para comprobar duración y fade.

`remote_learning_action` es una prueba determinista del observador de acciones
de enseñanza; no necesita red ni reproduce audio. Solo permite acreditar Play
o Stop con el ID de un comando enviado desde ese cliente en el paso activo,
su ACK aceptado y un snapshot posterior de la misma canción. Un cambio desde
otro cliente o el simple estado playing/stopped no sustituye esa evidencia.
La intención de un toggle desde reproducción es Pausa: una posterior
reanudación ajena no puede acreditarlo como el ejercicio de Play.
`learning_guide` comprueba además la secuencia de requisitos temporales al
retomar: Configuración → Control remoto → modo → conexión/disponibilidad →
cierre de Configuración cuando el ejercicio requiere el reproductor. Se
conserva el paso guardado; las condiciones no ejecutan esos controles.
Los pasos completos de Servidor/Cliente necesitan además una revisión manual
con dos equipos reales: abrir Configuración, recorrer sus controles,
establecer la conexión, recibir canción/onda/canales y comprobar Play/Stop.
Registrar por separado el resultado de CTest, la revisión visual de componentes
y el flujo integral de la app en dispositivos.

El 3 de octubre de 2026 se compilaron correctamente en
`build/codex-modal-macos` la app `MTControls_Standalone` y los targets
`MTControlsLearningGuideTests`, `MTControlsRemoteLearningTests`,
`MTControlsLearningOverlayTests` y `MTControlsSettingsPanelTests`. La ejecución
final de CTest aprobó `learning_guide` (0,55 s),
`remote_learning_action` (0,01 s), `learning_overlay` (2,22 s) y
`settings_panel` (0,16 s): 4/4, 100 %, 2,95 s total. La app y el target del
overlay se recompilaron correctamente después de los ajustes finales del
menú y cambio de modo; la suite incluye la regresión del menú embebido y del
texto pendiente sin duplicado.

La revisión manual parcial de escritorio comprobó el catálogo de cinco
cursos, Servidor pasos 1–7 y Cliente pasos 1–5 usando Configuración real.
Comprobó también la reanudación de Cliente paso 5 desde Local: abrir
Configuración y elegir Cliente, conservando el paso guardado; y el menú Más
embebido, con Cursos/Reiniciar accesibles y retorno al catálogo dentro del
panel. El recorrido de conexión y los ejercicios Play/Stop con dos equipos
siguen pendientes. Este resultado no acredita builds iOS/Android ni
instalaciones físicas.

`waveform_editing` usa los handlers reales de ratón y teclado del editor:
Copy, Cmd/Ctrl-C/V, arrastrar copias antes de su fuente, volver al origen del
gesto, recortar y ampliar, y Deshacer/Rehacer. Comprueba las coordenadas de audio,
la identidad de origen, el desplazamiento de las secciones siguientes y la
conservación de anuncios de utilidad. También ejecuta el mismo handler de guardado
que usa `Editar rango`, incluidos recorte, ampliación al final y traslado de ambos
extremos; no requiere abrir el diálogo nativo del sistema para esa comprobación.
`song_mix_export_timeline` verifica además
los límites de fuente de copias movidas, recortadas y sin su sección original.

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

## Validación local de Ensayo del 01/10/2026

- macOS arm64: pasaron `practice_panel_gestures`, `remote_session`,
  `remote_session_portable_transport`, `audio_engine_core` y `stem_mix_engine`.
  La sesión nativa se comprobó con `PW_REMOTE_DIRECT_CONNECT=1`; estos resultados
  acreditan transporte WebSocket local, no descubrimiento físico mDNS.
- Compilación iOS Simulator correcta. La cabecera y los selectores se revisaron
  visualmente en iPhone 17 Pro Simulator. La comprobación manual del último
  ajuste de enfoque de tres estados quedó pendiente al bloquearse la Mac;
  sus transiciones sí pasaron la prueba automatizada de gestos.
- iPhone 16 físico: compilación Debug firmada, firma validada, instalación y
  apertura por Wi-Fi confirmadas, con el dispositivo ausente del registro USB.
  Instalación y apertura no sustituyen una prueba manual de audio o gestos.
- Esta validación es específica de Ensayo. No se ejecutó aquí el gate completo,
  ni se verificó este conjunto de cambios en Android o Windows.

## Validación de selectores compartidos del 01/10/2026

- Compilaciones macOS Standalone, iOS Simulator y Android Debug correctas.
- Pasaron `input_controls` y `settings_panel`: opciones, selección visible,
  anclaje, navegación de Configuración y scroll de Ruteo.
- En iPhone 17 Pro Simulator se revisó Apariencia e idioma, se abrió el combo
  de temas y se alternó entre Verde Noche y Ámbar Estudio, restaurando el tema
  original. Se observaron bordes redondeados, check, color seleccionado y cierre
  al elegir, sin esquinas blancas.
- `input_controls_popup` devolvió skip: macOS no concedió foco al ejecutable
  de pruebas. Su contrato ampliado quedó compilado, pero no se acredita aquí
  ejecución automatizada de teclado. La herramienta de interfaz tampoco pudo
  ejecutar el clic por coordenadas sobre Instrumentos en Ensayo; ese menú usa
  el mismo dibujo global, pero su revisión visual específica queda pendiente.
- No se realizó instalación física ni validación de Windows en esta tarea.

## Validación de regla musical compartida del 01/10/2026

- Compilaciones macOS Standalone, iOS Simulator y Android Debug correctas.
- Pasó `practice_panel_gestures`, que incluye zoom, marcas musicales, cambios
  de tempo y métrica, y loop A–B.
- Se revisó la regla en macOS con edición desactivada y activada, conservando
  su alineación con la onda y el cursor. En iPad Pro de 13 pulgadas (M5)
  Simulator se instaló, abrió y revisó la vista con una canción cargada.
- La revisión visual de tablet acredita iOS Simulator; no se realizó revisión
  visual en tablet Android ni en dispositivos físicos para este cambio.

## Validación de importación M4A del 02/10/2026

- macOS Standalone y `MTControlsSongWizardTests` compilaron correctamente.
- Pasó `song_import_wizard` (2,91 s), con un encoder local disponible. Los
  fixtures AAC y ALAC recorrieron los cuatro pasos reales, se convirtieron a
  FLAC, generaron onda y metadata, y se registraron en Biblioteca. Los proyectos
  se reabrieron despues de eliminar exclusivamente los M4A del test aislado.
- Se comprobaron extension `.M4A`, originales intactos durante el guardado,
  rechazo de audio corrupto y limpieza de temporales. Un fallo no genero un
  proyecto ni una entrada de Biblioteca.
- La herramienta de interfaz devolvio timeout al consultar la app de macOS;
  el selector nativo no se reviso manualmente. La ejecucion en Windows/Linux
  queda pendiente; la conversion utiliza el mismo worker de escritorio.

## Validación de edición de waveform del 02/10/2026

- macOS Standalone y `MTControlsWaveformEditingTests` compilaron correctamente.
- Pasaron `waveform_editing` y `song_mix_export_timeline` (2/2). Los ocho casos
  del editor cubren fuente estable al mover a la izquierda, ida/vuelta del drag,
  copiar copias movidas/recortadas por botón y teclado, ripple de recorte y
  ampliación, recuperación de una copia al final, anuncios de utilidad y el
  guardado de Editar rango, con Deshacer/Rehacer.
- Los eventos de arrastre usan handlers reales; la prueba de rango invoca el
  handler compartido con Guardar. No se acredita revisión visual del diálogo
  ni instalación en dispositivos físicos en esta validación.

## Validación de Fin al editar secciones del 02/10/2026

- Reproducción antes de corregir: un WAV de 30 segundos, con `Fin` restaurado
  a 30, pasó a `Fin = 10` al publicar una anotación de 5 a 10 segundos.
- macOS Standalone compiló y pasaron `waveform_editing` y
  `song_mix_export_timeline` (2/2). Los 13 casos del editor incluyen cinco
  regresiones nuevas con `MTControlsAudioProcessor` real y audio temporal.
- Se comprueban marcador de inserción y seek por los handlers de la regla,
  anotaciones parciales, mover/recortar/Deshacer, `Fin` predeterminado y
  restaurado, conservación de un recorte manual, y extensión/reducción de la
  duración al insertar/eliminar una copia.
- Biblioteca, ajustes, cache y paquetes de assets del fixture usan la raíz
  temporal del test. Los pads se preparan localmente antes de construir el
  procesador para evitar descargas durante estas pruebas.

## Validación de automatización de secciones del 02/10/2026

- macOS Standalone Release y los targets afectados compilaron correctamente.
- El gate conjunto pasó 9/9 en 15,56 s: `remote_session`,
  `song_project_creation`, `waveform_editing`, `section_automation`,
  `audio_engine_core`, `pads_engine`, `song_mix_export_timeline`,
  `song_mix_exporter` y `stem_mix_engine`. Remoto usó sockets locales.
- Los ocho casos de `section_automation` verifican publicación concurrente,
  borradores dispersos, Save/Cancel/Clear, mezcla base, MIDI y propiedad remota,
  seguimiento tonal de Pads, huecos y varias fronteras en un callback largo.
  El loop y el salto usan transporte real y muestras del audio renderizado:
  repetir el mismo ID conserva el ajuste manual y el mute del origen no se
  propaga al destino. El fixture espera la generación de seek aceptada y la
  disponibilidad real de los buffers antes de evaluar el callback dividido.
- Guardado cubierto con proyectos, sidecar y paquetes Cloud aislados; la
  exportación reabre WAVs y compara automatización a distintas velocidades
  contra referencias que usan el mismo DSP y arrange.
- Se revisaron capturas de los componentes reales de la barra a anchos de
  360, 768 y 1200 puntos y del strip con su indicador A. Las pruebas también
  verifican que las acciones no se solapen. Capturas locales en
  `build/section-automation-qa/` mediante `PW_AUTOMATION_CAPTURE_DIR`.
- Esta revisión visual acredita componentes renderizados en macOS; no una
  sesión interactiva completa de la app. No se realizó compilación iOS/Android,
  instalación física ni validación de Windows/Linux para este cambio.
- Actualización del resaltado pulsante: Standalone recompilado y
  `waveform_editing` aprobado (3,71 s). Capturas actuales en
  `build/section-automation-pulse-qa/` muestran el borde, relleno e indicador A
  con el color del tema. El helper reemplaza los PNG anteriores al recapturar.
  La instancia interactiva conserva un borrador de Verso activo; su reinicio
  para comprobar la animación completa queda pendiente de cerrar ese borrador.

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
