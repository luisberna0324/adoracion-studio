---
type: Quality Contract
title: Pruebas funcionales de PlayWorship
description: Suite automatizada, aislamiento de datos y validación en dispositivos
tags: [tests, ctest, ci, audio, mobile]
timestamp: 2026-10-03
---

# Pruebas funcionales

## Suite automatizada

GitHub Actions no ejecuta pruebas ni validaciones de PR, tampoco manualmente.
Los workflows tests.yml, pr-semver.yml y android-cloudbuild.yml se retiraron;
solo ci.yml genera instaladores de release bajo workflow_dispatch explícito.
No se borró ninguna prueba de aplicación/backend. Ejecutar SemVer, recursos,
backend y PlayWorshipTests fuera de Actions mediante el pipeline local;
[LocalFirstCI](release/LocalFirstCI.md) documenta comandos y límites de Codex Cloud.
Los tests de archivos, MIDI, creación y exportación conservan raíz temporal
por proceso mediante tests/run_isolated_test.py. Un retorno 77 es fallo en
macOS; Linux sin display conserva skips en los tests de interfaz.
Codex Cloud coordina QA principal del mismo SHA que se entregará al workflow
de release; los entornos nativos complementan los targets no disponibles allí.

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
build/functional-tests --output-on-failure -L lan`. El gate local ejecuta
`remote_session` con Bonjour nativo y `remote_session_portable_transport` con
conexión WebSocket directa a loopback; este último ejercita comandos, snapshots,
onda, VU y reconexión con la compilación portátil.

## Cobertura de la aplicación nativa

Los consumidores de `MTControls` heredan `MTCONTROLS_USE_BUNGEE` y
`SOUNDTOUCH_FLOAT_SAMPLES`, además de los headers de SoundTouch para el backend
alternativo. Los headers públicos del procesador deben compilarse con el mismo
backend y tipo de muestra que la biblioteca para conservar su layout en memoria.

`standalone_audio_runtime` valida dispositivos simulados de 2, 8 y 64 salidas,
restauración de preferencias, entradas deshabilitadas antes de abrir audio y
parada/reinicio. Se ejecuta con `ctest --test-dir <build> -R standalone_audio_runtime`.
Usa además el procesador real, su bridge y almacenamiento temporal aislado para
comprobar el ancho del bus sin entradas, rutas al último par, persistencia y
cambios de salidas activas. Conserva compatibilidad mono/estéreo y rechaza
layouts fuera de los límites del procesador. El procesador sintético verifica
señal por canal y número de callbacks; no sustituye la negociación del real.
Ocho casos adicionales miden clic y guía reales después del callback del player:
mono Out 1/Out 2, tempo/tono originales y modificados, señal en destino y cero
exacto en el opuesto. Se verifican defaults sobre snapshots estéreo, stems
Click/Guide con categoría Other, herencia y persistencia tras reinicio, y
defaults estéreo reducidos a mono con una salida activa. `settings_panel`
comprueba que un dispositivo de ocho salidas solo ofrezca sus canales activos
(dos, uno y cuatro) y deshabilite estéreo con un único canal.
No sustituye pruebas físicas de audio/MIDI ni comprobaciones móviles de UI.

## Cobertura por contrato

| Contrato | Tests de regresión |
| --- | --- |
| [Transporte](transport/Transport.md) y [arrange](engine/SectionScheduler.md) | `audio_engine_core`, `song_mix_export_timeline`, `waveform_editing` |
| [Automatización por sección](engine/SectionAutomation.md) e [interfaz](ui/SectionAutomation.md) | `section_automation`: publicación coherente, valores dispersos, base y overrides temporales, límites dentro del callback y rampas; `waveform_editing`: barra, borrador fijado y historial; `song_project_creation`: codec, paquete, sidecar y Cloud; `remote_session`: propiedad y reconciliación |
| [Motor de audio](engine/StemMixEngine.md), click y pads | `stem_mix_engine`, `stem_buffering_source`, `metronome_engine`, `pads_engine`, `bungee_processor` |
| [MIDI](midi/MidiBindings.md) | `midi_settings` comprueba persistencia, modos, límites y disparos de CC/Note |
| [Creación de canciones](importing/SongCreation.md) | `song_project_creation`, `song_import_wizard`, `portable_flac_conversion`, `mobile_import_staging` |
| [Importación de proyectos desde disco](importing/LibraryPersistence.md#importar-un-proyecto-desde-disco) | `project_import`: respuesta del message loop, una sola activación, errores/reintento, copia privada y destrucción segura durante carga; `song_project_creation`: avance real durante extracción paralela y preservación del paquete |
| [Configuración móvil](ui/MobileLayout.md) | `settings_panel`: navegación agrupada, retorno con scroll conservado, controles remotos entre páginas, acciones de cuenta y alcance del último selector de ruteo |
| [Guías interactivas](ui/LearningGuide.md) | `learning_guide`: cinco cursos, condiciones remotas, requisitos temporales de sesión al reanudar, IDs, persistencia, reinicio y entradas inválidas; `learning_overlay`: spotlight, interacción y scroll, botones compactos, bloqueo de avance por botón/teclado, reparenting sobre Settings real y targets de Ensayo; `settings_panel`: acceso, destinos y scroll de aprendizaje; `remote_learning_action`: comando propio, distinción Play/Pausa, ACK y snapshot posterior, respuestas ajenas/atrasadas, rechazo, timeout y limpieza de evidencia |
| [Ensayo por instrumento](ui/MobileLayout.md) | `practice_waveform_mapping`: proyección de picos con el scheduler real, copias/recortes, desplazamientos, silencio fuente, límites e identidad de caché; `practice_panel_gestures`: gestos en toda la tarjeta, enfoque de tres estados, arrastre e inercia, zoom, tono y tempo, regla y snap de A/B, limpieza de loops, geometría de onda, reloj visual e invalidación/rechazo de entregas de onda obsoletas; `stem_mix_engine`: ganancias temporales y transiciones de audio; `remote_session`: propiedad, comandos pendientes y reconciliación |
| [Cloud Sync](integrations/CloudSync.md) y versiones de ajustes | `cloud_settings_sync_state`, `cloud_sync_index`, `settings_versions_sheet` |
| [Exportación](audio-systems/SongMixExporter.md) | `song_mix_export_timeline`, `song_mix_exporter` |
| [Control remoto LAN](integrations/RemoteControl.md) | `remote_session`, `remote_session_portable_transport` en CI; `remote_session_portable` y `remote_discovery_interop` en una LAN |

`remote_section_control` usa WebSocket real en loopback y el helper de dispatch
de producción: aceptación/ACK/snapshot, nombres de coro repetidos, IDs de
ocurrencia, canción/arreglo atrasados, destinos filtrados o ambiguos, carga en
curso, contexto visual diferido, época de servidor, compatibilidad v1 explícita
y feedback de rechazo. Usa un doble de estado
del procesador; la prueba de frontera usa el scheduler real, sin acreditar
prefetch ni render de audio. `section_automation` prueba además el procesador
real por ID, validación sin efectos y Loop→Ir con audio; `waveform_editing`
comprueba mapeo de las tarjetas reales y feedback en una sección distinta.

En la revisión de nube del 13/10/2026, el target nuevo y `remote_session`
pasaron mediante un build aislado con JUCE 8.0.12 e IXWebSocket fijado al SHA
del proyecto. `audio_engine_core` también pasó. La configuración completa de
la app inicialmente quedó bloqueada por FreeType y Bungee ausentes. Se resolvió
con dependencias Debian verificadas, extraídas en una raíz temporal sin ejecutar
scripts de instalación, y `scripts/bootstrap_bungee.sh` con un destino temporal
nuevo: Bungee público 2.4.24, archivo y licencia verificados contra los hashes
fijados por el repo. No se compró ni se aceptó una licencia adicional.

El build nativo usó el JUCE del workflow (`501c07674e1ad693085a7e7c398f205c2677f5da`)
y Bungee real. El build por defecto con GCC 14 falla en la expresión preexistente
`juce::jmax<juce::int64>(1, bytes)` por instanciar SIMDNativeOps de `long long`.
Con `JUCE_USE_SIMD=0`, sin cambiar el código ni sustituir Bungee, compilaron
`MTControls_Standalone` y los cinco targets relevantes de test. La app abrió
durante ocho segundos en Xvfb/Openbox y fue detenida de forma controlada. Es un
smoke de arranque: las variables XDG temporales no aíslan por completo este
JUCE en Linux; su consulta de sesión estándar no encontró un snapshot. Los
tests de archivos sí usaron sus wrappers de almacenamiento temporal aislado.

En el gate nativo pasaron `remote_session`, `remote_section_control` y
`audio_engine_core`. `waveform_editing` y `section_automation` no quedaron
verdes: comparación de repintado (delta de un canal), desplazamiento del toolbar
de automatización, silueta de onda fuente desplazada y duración de la voz de
guía pendiente. Los cuatro fallos se reprodujeron con `main` limpio `5a425c0`
en un worktree separado, mismo SDK/configuración/escritorio, por lo que no son
regresiones de este cambio. No se corrigieron esos contratos ajenos al alcance.

Los casos nuevos nativos de identidad/validación y de indicador por tarjeta
pasaron, al igual que los casos principales de Loop→Ir, preparación fría y
mezcla en callbacks divididos. `PW_LIVE_CAPTURE_DIR` genera
`remote-section-update-required.png` con la tarjeta real mostrando directamente
`Actualiza servidor`; fue revisada visualmente en Linux. Esto acredita render
de componentes y ejecución nativa Linux escalar, no compilación con SIMD,
dispositivo iPad/Mac ni la causa del reporte del tester. La validación física
Apple y la atribución a una versión concreta siguen pendientes.

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

El fixture de edición activa el botón mediante el handler MIDI real y espera,
con un límite de un segundo, que JUCE procese su clic asíncrono antes de iniciar
los gestos. Si no entra en edición, falla explícitamente; no fuerza el estado
del botón ni omite las comprobaciones del punto de inserción y Fin.

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

## Ajuste de indicaciones sobre la onda del 03/10/2026

- macOS Standalone Release compilado. Las cuatro suites afectadas pasaron
  (4/4, 12,03 s): `waveform_editing`, `song_project_creation`,
  `section_automation` y `song_mix_export_timeline`.
- Se verifica borrado de un punto coincidente mediante el botón real y doble
  clic, Deshacer con el ID/orden/anchor original, arrastre sin seek y sin
  publicaciones parciales, y conservación de copia, recorte y automatización.
- Las etiquetas permanecen sobre la onda, se apilan sin intersección y dejan
  libres los controles de sección. Los compases/pulsos/cursor de la regla se
  comparan por píxel antes y después de agregar una indicación.
- Breakdown y A capella se prueban como indicaciones inmediatas, también en
  migración legacy sin flag cuando el mapeo se puede conservar con seguridad.
- Capturas de componentes reales revisadas en `build/cue-wave-overlay-qa/`:
  escritorio, formato de teléfono y tooltip pequeño. Son renderizados de
  los componentes en macOS; no acreditan instalación ni QA física móvil.
- Logs: `build/cue-wave-overlay-build.log` y `build/cue-wave-overlay-tests.log`.

## Validación de indicaciones independientes del 03/10/2026

- macOS arm64 Standalone Release y los targets afectados compilaron en
  `build/codex-modal-macos`. Gate final 7/7 (100 %, 12,91 s):
  `song_project_creation`, `waveform_editing`, `section_automation`,
  `audio_engine_core`, `pads_engine`, `song_mix_export_timeline` y
  `song_mix_exporter`.
- El editor comprueba arrastre sin seek ni publicaciones parciales, historial,
  borrar sin eliminar el verso, copiar/pegar, mover la copia hacia la izquierda,
  recortar/Deshacer, reasignación al dividir, agrupación táctil también en los
  bordes, marcas fuera del viewport y secuencias a través de cambios de BPM.
  Copias/pegados que exceden 512 anuncios se rechazan sin ripple ni commit.
- Los tests con procesador y WAV reales esperan el seek aceptado al iniciar
  Play antes de verificar el cruce de loop/jump, incluyendo posición del
  destino y offsets de los anuncios. Cubren pausa, seek, frontera exacta del
  callback, Ensayo A–B, cue-only, `Break` al recargar idioma y mute/unmute sin
  reactivar una cola anterior. Exportación reabre el WAV y mide los offsets.
- Codec, proyecto, sidecar y fingerprints Cloud conservan vínculos y offsets
  variables. Migración legacy conserva casos ambiguos; remoto acepta 512 y
  rechaza 513 anuncios después de expandir/migrar, mientras disco conserva el
  documento completo.
- Capturas de los componentes reales en `build/cue-marker-qa/`, generadas con
  `PW_CUE_CAPTURE_DIR`: regla de 1200 puntos, teléfono de 390 puntos y popover
  de detalle con controles de 44 puntos. Se revisaron visualmente; acreditan
  renderizado en macOS, no una instalación ni QA física de iOS/Android.
- La app anterior estaba reproduciendo durante la validación; no se reinició
  esa sesión. La compilación nueva quedó disponible para la siguiente apertura.
  Logs locales: `build/cues-final-build.log` y `build/cues-final-tests.log`.

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

## Resultado de la migración nativa del 01/10/2026

- Linux x86_64, Debug, fuente base `b0e83fddddae28cd313c4e4b27d0b72b80c4e0e0`:
  `MTControls_Standalone` y `PlayWorshipTests` compilaron correctamente
- Gate con display real del escritorio cloud: 26/26 pruebas pasaron, sin
  fallos ni skips, con `-LE 'interactive|lan' --parallel 2` (7.95 s)
- La app abrió su pantalla de bienvenida. Una segunda invocación terminó
  con código 0 sin crear otra ventana
- `input_controls_popup` quedó excluido por ser interactivo. Audio/MIDI
  físicos, login, flujo completo de proyecto/salida y plataformas Apple,
  Windows y Android siguen pendientes de validación específica


## Teams

Los targets `MTControlsTeamsLocalStoreTests`, `MTControlsTeamsPreparationTests`,
`MTControlsTeamsPanelTests` y `MTControlsTeamsWorkspaceTests` usan almacenamiento
isolado y cuentas ficticias. No crean equipos ni contenido en producción.

- Caché: aislamiento de cuentas, exclusión de credenciales/URLs, identidad
  estable por revisión, concurrencia y fallo conservador ante corrupción.
- Descarga: fixtures FLAC reales, recuperación de archivos, cambios locales,
  idempotencia, versiones separadas y conservación de la lista activa. No se
  crean setlists ni se publica contenido.
- Interfaz: Configuración → Teams oculta por defecto, habilitada por capacidad
  explícita y dedicada solo a usuarios. Miembros y capacidades revocadas no
  acceden a la administración. Confirmaciones y carga se prueban en distintos
  anchos; no existe catálogo ni acción de descarga dentro de Teams.
- Biblioteca: las canciones descargadas quedan en la biblioteca habitual sin
  selectores de espacios ni alterar la cola de reproducción.
  Las filas Nube distinguen procedencia incluso con IDs remotos repetidos y
  resuelven descargas compartidas por cuenta/fuente/revisión. No ofrecen acciones
  de escritura hacia la nube del propietario; descargan y se agregan al setlist
  mediante el selector habitual.

`MTControlsTeamsUIPreview` guarda escenarios de administración, confirmación,
carga y perfil denegado. `PW_TEAMS_PREVIEW_DIR` fija la carpeta al
usar `run_isolated_test.py`. Estas capturas acreditan los componentes y datos
simulados; no una transferencia real ni una instalación física.

El backend tiene pruebas de perfil de suscripción, permisos, usuarios,
catálogo Cloud automático, descarga y retirada de acceso. La API no acepta
publicaciones, coordinadores, invitaciones ni setlists compartidos. Validar
Cloud Run, R2 con dos cuentas reales e iOS/Android son etapas separadas.
El catálogo unificado también prueba fuentes compartidas sin plan propio,
búsqueda, paginación, colisiones y retirada de permisos entre páginas.


### Onboarding de nube compartida

`learning_guide` cubre los tres pasos, bloqueo sin acceso y sin audio descargado,
postergación y reanudación persistente. `teams_local_store` verifica identidad
por cuenta, fuente y alta; `learning_overlay` verifica acciones y capturas de
la bienvenida y de cada paso en teléfono y escritorio. Las pruebas gráficas
que abren menús nativos requieren acceso a WindowServer de macOS.
El backend prueba alta estable con nube vacía, baja/reingreso con nueva identidad
y compatibilidad de membresías anteriores sin escrituras.

## Recursos binarios de Android

`cmake/PlayWorshipAssets.cmake` es la lista compartida de recursos: escritorio e
iOS siguen usando `juce_add_binary_data`; Android genera `BinaryData.h`, la tabla
de nombres y un `.cpp` por archivo dentro del directorio de build. Las copias
históricas `android/app/src/main/cpp/BinaryData*` ya no se compilan ni se incluyen.
La generación solo requiere CMake; detecta cambios de contenido/tamaño y conserva
los bytes originales, incluidos `google-g.png` y `LuzDeEsperanza.pwproj`.
Los nombres deben producir identificadores únicos con la conversión de JUCE y
comenzar con una letra o guion bajo; los conflictos fallan en configuración.

Prueba rápida sin JUCE ni Android SDK:

```bash
cmake -S tests/binary_resources -B build/binary-resources -G Ninja
cmake --build build/binary-resources --parallel 3
ctest --test-dir build/binary-resources --output-on-failure --no-tests=error
```

Compara cada recurso con su archivo original, tamaños, nombres, terminadores y
búsquedas directas/dinámicas; incluye el icono de Google y el paquete demo, además
de consultas inexistentes/nulas. Otra prueba comprueba regeneración al modificar
bytes/tamaños o añadir/quitar recursos, y que no se reescriban en un build sin cambios.
El target local `resources` ejecuta estas pruebas antes de integrar cambios. No sustituye compilar la APK ni probar el
acceso habitual y la instalación de la demo en un dispositivo Android.

## Tarjetas Live y onda fuente — 08/10/2026

- `waveform_editing` y `touch_waveform_navigation`: 2/2 pruebas pasaron en
  `build/codex-modal-macos` (5.02 s, tras el ajuste de ancho completo).
- La regresión de render inserta dos copias antes de secciones originales de
  una fuente de 30 s. Comprueba que los originales desplazados más allá del
  final de la fuente conservan la silueta de su onda y su rango de audio.
- La navegación cubre anchos de 568, 844, 956, 1024 y 1200 puntos, márgenes del notch
  a ambos lados, arrastre, rueda, asentamiento del trackpad y un toque que
  interrumpe ese asentamiento. Las tarjetas quedan completas al detenerse y
  sus acciones conservan un área de 44 × 44 puntos.
- En teléfono, la señal real llega a ambos bordes físicos (considerando el
  paso de dos píxeles de sus barras), sin descontar los insets del dibujo.
  Las acciones quedan dentro del área segura con cutouts de 44 y 62 puntos,
  al inicio, en posiciones intermedias y al final. Cambiar sólo los insets de
  lado también actualiza sus límites. Las capturas de los componentes en
  956 puntos verifican ambos extremos y ambas orientaciones.
- La compilación Debug para iPhone Simulator terminó correctamente; la app se
  instaló y abrió en el simulador iOS 26.5. La revisión visual de Forever Reign
  confirmó Coro, Vamp, Puente y Final completos, con onda visible en los cuatro.
  Se conservaron los bins de audio existentes. Esta revisión no acredita una
  instalación física ni validación visual de iPad o Android.
- La compilación posterior con secciones a ancho completo también se instaló
  y abrió en ese iPhone Simulator; la revisión visual confirmó la fila sin
  bandas laterales, con etiquetas y controles fuera del cutout.


## Selector de instrumentos de Ensayo — 09/10/2026

`practice_panel_gestures` incluye recorridos desde los handlers reales de
`PracticeInstrumentSelector`: preview/confirmación única (incluido release
coalesced), ausencia de seek/pausa al recorrer instrumentos, retarget por ID,
gestos verticales de tres niveles y propiedad del eje, pinch en ambos órdenes,
magnificación, reordenación antes del sondeo, listas vacías/únicas y cancelación
por canción, contexto, desconexión o tamaño. También cubre nombres completos
para accesibilidad, layout en 320/390/768 puntos, spacing sin cambios en
Tono/Tempo y destrucción síncrona desde el proveedor de onda.

Estas pruebas JUCE nuevas no se ejecutaron en el entorno cloud de esta revisión:
no dispone de JUCE ni CMake. Los harnesses portables ejecutan código real con
dobles de UI y no acreditan compilación nativa, accesibilidad del sistema,
elipsis renderizada ni revisión visual/táctil en iOS/Android. Antes de publicar,
compilar `MTControls_Standalone`, ejecutar `practice_panel_gestures` y revisar
el carrusel con nombres largos y gestos interrumpidos en la app real.


## Ensayo: halo y acompañamiento

- `practice_accompaniment` prueba la lógica real de selección y comandos de
  acompañamiento sin depender de JUCE. Los dobles de sesión comprueban guía
  original/dinámica/ausente, índices de stems incluidos, independencia de los
  buses y compuertas enable/división/mute del metrónomo. Es cobertura del contrato
  de control; no sustituye un render ni una prueba de audio del motor real.
- `practice_toggle_request` prueba el guard real usado por el panel: confirmación,
  estado remoto anterior, timeout de 2 segundos, cambio de contexto, independencia
  entre botones y wrap del contador de milisegundos.
- `practice_panel_gestures` cubre geometría 320/390, estado checked/accesibilidad,
  toque/arrastre/teclado, reconciliación asíncrona, A–B/errores reubicados, restauración
  desde ayuda y halo exterior en los tres estados. Requiere JUCE y entorno GUI.
- Validación manual pendiente de dispositivo: comparar la intensidad del halo
  estático, legibilidad de Guía/Metrónomo y avisos, VoiceOver/TalkBack, foco por Tab,
  latencia real del servidor y escucha audible de cada fuente. Confirmar que la
  onda mantiene su tamaño y los gestos/doble toque siguen disponibles fuera de `?`.

Comprobaciones pequeñas sin JUCE (no equivalen a `MTControls_Standalone`):

```bash
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/practice_accompaniment_tests.cpp -o /tmp/practice-accompaniment
/tmp/practice-accompaniment
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/practice_toggle_request_tests.cpp -o /tmp/practice-toggle-request
/tmp/practice-toggle-request
```

La regresión waveform_editing pasa explícitamente la preferencia de reducir movimiento al seguimiento de Secciones: prueba transición de 280 ms y centrado inmediato por separado, independientemente del ajuste de accesibilidad del runner. El runtime sigue consultando la preferencia del sistema.

## Canción en blanco — nube Linux, 15/10/2026

Base `origin/main` y `v2.3.3^{}`:
`f31df144c84f4bd8f225f6c563495844531d9be8`. Trabajo en `feat/blank-song`,
sin incorporar ramas Android experimentales ni utilizar el Mac.

- Linux x86_64 / GCC 14, Debug, JUCE 8.0.12 (checkout de CI
  `501c07674e1ad693085a7e7c398f205c2677f5da`) y Bungee 2.4.24 real.
  El bootstrap verificó el archivo y la licencia por SHA-256. Se configuró
  `JUCE_USE_SIMD=0`, por compatibilidad con GCC 14; no se usaron stubs.
- `MTControls_Standalone` y los targets del gate compilaron. La app arrancó
  en Xvfb. El entorno no tiene dispositivos físicos de audio/MIDI; los
  asserts/avisos de ALSA y las capturas no acreditan escucha ni latencia real.
- Gate de 13 suites: **12 aprobadas, 1 fallida**, en 50,92 s. Pasaron
  `blank_song_transport`, `song_project_creation`, `song_import_wizard`,
  `section_automation`, `remote_session`, `remote_section_control`,
  `audio_engine_core`, `metronome_engine`, `pads_engine`,
  `song_mix_export_timeline`, `song_mix_exporter` y `stem_mix_engine`.
- `blank_song_transport` procesa bloques con el procesador, metrónomo,
  PadsEngine y scheduler reales y cero lectores de stems. Cubre libre después
  de una hora, pausa/reanudación/stop, cuatro combinaciones click/pad, parada
  natural, loop, Ir por ID, rechazo de ID inválido, 3/4 y 6/8, ratio de tempo y
  posición remota con duración abierta. El test de fase usa el click sintético
  existente del motor, con su duración conocida, independiente del sample que
  pueda estar instalado.
- Persistencia prueba `.pwproj`, Biblioteca, setlist, codec anterior sin flag,
  duración inválida y conservación independiente del pad al guardar click.
  Reabre paquetes sin `audio/` ni `waveform.bin`. El wizard recorre la creación
  real y verifica cambios de BPM con compases e IDs conservados.
- La nueva comprobación de timeline sin bins pasó dentro de `waveform_editing`.
  La suite sigue fallando en dos asserts anteriores: diferencia de un nivel
  RGB en el repaint parcial de waveform y desplazamiento del ancla de la barra
  de automatización al hacer scroll horizontal. Ambos se reprodujeron con el
  `WaveformBar.cpp` original de `origin/main`, compilado contra el mismo harness
  y headers en nube; no se relajaron esos asserts. Esto compara el renderer
  original, no acredita una compilación completa de `main` en otras plataformas.
- Se corrigió el aislamiento de `song_mix_exporter`: su AssetBootstrapManager
  ahora se compila con la raíz temporal, igual que los otros tests de I/O.
  El fixture de guía larga usa una ruta distinta al sample corto ya calentado.
  No se cambiaron los motores de exportación ni de guía para hacer pasar tests.

Logs locales: `build/blank-song-final-tests.log`,
`build/blank-waveform-baseline-tests.log`, `build/blank-song-build6.log` y
`build/blank-song-build7.log`. Quedan pendientes audio/MIDI físicos y
compilación/QA de macOS, iOS, Android y Windows. No se ejecutaron Actions,
push, PR, publicación ni despliegue.

### Alcance simplificado posterior

El diseño anterior queda recuperable en el commit `11f804d`. Se retiraron sus
formularios especiales y sus herramientas de capturas; la entrega de capturas
se canceló. La creación actual se prueba desde el botón `Crear canción en blanco`
en Detalles, con transacción real `.pwproj`, bloqueo de reentrancia, valores
vacíos por defecto, cancelación del destino y retorno al flujo de importación.
El test verifica el botón sin solapamientos a 320, 390, 768 y 1200 puntos.

El transporte mantiene el pad global habitual al pausar/detener y carga sin
sobrescribir click/pad. Los tests de compases estructurados siguen comprobando
compatibilidad con paquetes del prototipo; ya no describen una opción del wizard.
La regla en segundos y el crecimiento están sólo analizados, no implementados.
Verificación posterior: Standalone Linux y los 13 targets del gate compilaron
con el mismo JUCE/Bungee real. Gate: **12/13 suites aprobadas**, 51,23 s. Pasaron
la nueva creación directa, persistencia, transporte sin stems, automatización,
remote, motores de audio/click/pads y exportadores. `waveform_editing` conserva
los dos fallos de renderer ya contrastados con `main`; la nueva prueba sin bins
sigue pasando. No se modificó `WaveformBar.cpp` en esta simplificación.

Logs: `build/blank-song-simplified-build.log`,
`build/blank-song-simplified-build2.log`,
`build/blank-song-simplified-tests.log`. Esta verificación no prueba el crecimiento
propuesto ni sustituye audio/MIDI físico y QA en dispositivos/plataformas reales.

### Alcance final: libre, sin secciones ni contenido de onda

La decisión final sustituye el prototipo estructurado: el botón de Detalles
sigue guardando y cerrando el asistente, pero toda canción en blanco se reproduce
libre hasta Detener. Se retiraron el cómputo de duración por compases, las
secciones de respaldo y el contenido visual de la zona de onda. Los campos y
arreglos históricos del prototipo siguen siendo legibles, pero no se ejecutan
ni fijan duración. El análisis de timeline ampliable quedó fuera de esta versión
y está recuperable en `a25bcd9`; no hay una fase de crecimiento pendiente.

Verificación final en nube Linux, GCC 14 / JUCE 8.0.12 / Bungee 2.4.24 real,
`JUCE_USE_SIMD=0`:

- Standalone y todos los targets del gate compilaron; una segunda comprobación
  de Ninja confirmó que los binarios correspondían al código actual.
- Gate de 13 suites: **12 aprobadas, 1 fallida**, 50,66 s. Pasan
  `song_project_creation`, `song_import_wizard`, `blank_song_transport`,
  `section_automation`, `remote_session`, `remote_section_control`,
  `audio_engine_core`, `metronome_engine`, `pads_engine`, `stem_mix_engine`,
  `song_mix_export_timeline` y `song_mix_exporter`.
- El test nuevo de UI pasa dentro de `waveform_editing`: área de onda uniforme,
  sin hijos visibles, bins, regla, cursor, texto de compases ni acciones de
  sección, a 320/390/768/1200 puntos; menú táctil y selector de escritorio
  ocultos, callbacks antiguos bloqueados y restauración de la vista/secciones
  normales al salir de la vacía. Usa componentes JUCE reales en Xvfb; no acredita
  interacción táctil en dispositivos físicos ni compara la imagen de referencia.
- El transporte real procesa sin stems ni secciones en el scheduler, mantiene
  reproducción después de Seek a una hora, pausa/reanuda y Stop vuelve a cero.
  Se verifican click/pad independientes, pad global durante pausa/stop, cambios
  de pitch/tempo, métricas 3/4 y 6/8 sin duración, metadata histórica que no
  activa partes ni Fin, y órdenes remotas ordinarias de tempo/tono/click/pads/
  play-pause/stop. Estas últimas comprueban el adaptador y sus comandos; las
  suites remotas existentes además ejercitan loopback y controles normales.
- Persistencia reabre `.pwproj` sin `audio/` ni `waveform.bin`, registra en
  Biblioteca/setlist y conserva la lectura de metadata histórica sin convertir
  las referencias antiguas sin audio en canciones reproducibles.
- `waveform_editing` sigue fallando sólo en los dos asserts conocidos: repaint
  parcial con diferencia de un nivel RGB y ancla de automatización tras scroll.
  Son los mismos fallos reproducidos antes con el renderer de `origin/main`;
  la nueva prueba de canción vacía pasa. No se relajaron esos asserts.

Logs: `build/blank-song-free-build3.log`,
`build/blank-song-free-build-final.log`, `build/blank-song-free-tests.log`.
No se usaron stubs para compilar o validar audio. No hay dispositivos físicos
de audio/MIDI; quedan sin verificar escucha, latencia, tacto y builds de macOS,
iOS, Android y Windows. Todo se ejecutó en nube: sin Mac, push, PR, Actions,
publicación, despliegue ni incorporación de los experimentos Android excluidos.

### Crear canción en blanco desde la tarjeta de Detalles

Sobre `fb65c0c`, la tarjeta de información de metadatos pasa a ser la única
acción `Crear canción en blanco`, con la descripción aprobada
`Empieza sin pistas de audio y usa el clic y los pads.`. Se retira el botón
separado y su reserva de espacio al pie. La tarjeta es un botón JUCE con nombre,
descripción accesible y foco de teclado; se desplaza dentro del formulario.
Se conserva la transacción de creación, los campos y Siguiente para importar
audio. No cambian modelo, motores, canvas del reproductor ni control remoto.

Verificación en nube Linux, Debug, JUCE 8.0.12 y Bungee 2.4.24 real con
`JUCE_USE_SIMD=0`:

- Compilaron `MTControls_Standalone` y los targets preparados para la
  verificación. Arranque real del Standalone en Xvfb; crear desde la tarjeta
  vuelve a Biblioteca y genera un `.pwproj` con sólo `manifest.json`, cero
  stems, nombre y BPM introducidos. Se comprobó también su carga.
- **3/3 suites aprobadas**, 11,24 s: `song_import_wizard`,
  `song_project_creation` y `blank_song_transport`. El asistente cubre importación
  normal y edición, WAV/FLAC/M4A según disponibilidad local, persistencia real,
  cancelación, reentrancia y cierre después del guardado.
- El test de creación en blanco comprueba una sola acción, el texto y la
  descripción aprobados, tamaños 320/390/768/1200, posiciones separadas de los
  campos y acceso por scroll con viewport corto en geometría móvil/escritorio.
  Mantiene las comprobaciones de guardado/reapertura, valores vacíos por
  defecto, cancelación de destino, navegación normal e invisibilidad al editar
  o salir de Detalles.
- Se revisaron renders nativos de la tarjeta/formulario a esos cuatro anchos
  y la vista de Detalles del Standalone. Son verificaciones Linux de componentes
  reales; no acreditan teclado, safe-area ni interacción táctil en dispositivos.

Logs locales: `build/blank-song-metadata-card-build.log`,
`build/blank-song-metadata-card-tests.log` y
`build/metadata-card-app-smoke.log`. Renders de QA en
`build/metadata-card-preview/`. No se repitió el gate completo de audio/UI de
la etapa anterior: sus dos fallos conocidos de `waveform_editing` no se reparan
ni se vuelven a acreditar con este cambio de formulario. No hubo audio/MIDI
físico ni builds de macOS/iOS/Android/Windows. Sin Mac, push, PR, Actions,
publicación ni despliegue.

## Clic único al iniciar y reanudar (2026-10-15)

Investigación sobre `d1a0881` de `feat/blank-song`, sólo en nube Linux. El
`MetronomeEngine.cpp` previo era idéntico al de `origin/main` (`f31df144`);
main también publicaba triggers manuales desde Play y al activar el clic.
El defecto no dependía de crear una canción en blanco.

Se confirmaron tres mecanismos con el motor/procesador reales:

- Un trigger manual publicado tras el primer callback agregaba otro ataque
  256 frames después (5,33 ms a 48 kHz). Reanudar/activar/repetir Play justo antes
  del beat produjo dos ataques a 0 y 289 frames, separados 6,55 ms a 44,1 kHz,
  tanto con stems como sin ellos. El ajuste de la captura woodblock a dos copias
  de su propio sample tuvo residuo relativo menor que `7e-7`; no era sólo la cola.
- Al coincidir un beat con el borde del callback se disparaba en el último
  frame del bloque anterior y de nuevo en el primero del siguiente. En la
  captura a 48 kHz ocurrió en los frames 23999/24000.
- Con Play y el callback concurrentes se reprodujo un arranque doble 0/256 en
  una de 500 iteraciones, incluso tras retirar los triggers manuales. El clic
  podía consultar playing después de que ese callback ya hubiera tomado un
  snapshot detenido, sin avanzar el reloj. También se cubre de forma
  determinista la publicación parcial de Play entre el espejo y TransportClock.

El clic usa ahora la grilla y el snapshot de playback del reloj. Renderiza el
frame actual antes de avanzar, incluyendo subdivisiones exactamente en el
inicio de un callback. La API de preview manual se conserva. No cambian samples,
acentos, mezcla, ruteo, pad, persistencia ni interfaz.

`metronome_engine` compara muestras entre render continuo y callbacks de
64/256/257/1024 frames, a 1024/44100/48000 Hz, con negras, corcheas y
semicorcheas independientes/combinadas. Comprueba un ataque por beat y mantiene
las pruebas previas de publicación/reset/rangos. `metronome_transport` usa un
WAV de un frame como click de medida cargado por el motor real; no es un stem
silencioso ni reemplaza el motor. Comprueba arranque, pausa, reanudación,
activación, Play repetido, Stop y reinicio rápido en canciones con/sin stems;
Loop e Ir con destino a 100 frames del beat; 500 publicaciones concurrentes
con cero arranques dobles; y publicación parcial determinista. El stress publica
el mismo estado final que Play mediante el acceso de test para evitar reabrir
un dispositivo host ausente en cada iteración. Los demás casos llaman a Play.

Para capturar también la salida real con el woodblock incluido, ejecutar el
binario `MTControlsSectionAutomationTests --click-only` mediante
`tests/run_isolated_test.py`, con `DISPLAY` y
`PLAYWORSHIP_CLICK_CAPTURE_DIR` apuntando a un directorio de salida. El binario
`MTControlsMetronomeTests --capture <directorio>` conserva escenarios de
caracterización del motor; sus peticiones manuales deliberadas son previews,
no el comportamiento corregido de Play. Las capturas son PCM de la salida del
procesador/motor, sin grabador de pantalla ni dispositivo físico.

Resultado final: `MTControls_Standalone` compilado en Linux Debug con JUCE
8.0.12, Bungee 2.4.24 real y `JUCE_USE_SIMD=0`. Gate dirigido **12/12**, 57,06 s:
`remote_session`, `remote_section_control`, `song_project_creation`,
`song_import_wizard`, `section_automation`, `blank_song_transport`,
`metronome_transport`, `audio_engine_core`, `metronome_engine`, `pads_engine`,
`bungee_processor` y `stem_mix_engine`. Se recompilaron los targets antes de
la pasada final; una pasada preliminar no había encontrado el ejecutable de
`bungee_processor`, que se compiló y pasó en el gate final.

Después de corregir, las capturas con/sin stems tienen sólo el ataque esperado
en el frame 290, y la captura de borde sólo en el 24000. El ajuste a una copia
del woodblock tiene residuo relativo menor que `6e-7`. Arranque y reinicio desde
Stop producen un ataque inicial; pausa y Stop producen silencio del clic.
Capturas, análisis y logs locales en `/workspace/scratch/click-investigation/`.
No se ejecutó la suite completa de UI ni se atribuyen a este cambio los fallos
previos de `waveform_editing`. No se usó un dispositivo de audio/MIDI físico,
ni se verificaron macOS, iOS, Android o Windows. Sin Mac, push, PR, Actions,
publicación ni despliegue; se conserva la tarjeta aprobada de `d1a0881`.

### Estabilidad de tempo y coste del clic

Revisión ampliada autorizada por el usuario después del checkpoint `0a79567`:

- En una canción en blanco a 126 BPM llevada a 94,5 BPM se estaba renderizando
  el bus vacío de stems con Bungee y compensando esa latencia inexistente.
  La captura de medida empezó en el frame 146230 (3,32 s tarde) y en 12 s
  registró 20 ataques con seis pares próximos. Omitir sólo el render sin stems
  produjo 19 ataques desde cero, sin pares próximos y con error máximo de un
  frame. El cálculo de consumo fuente y el procesamiento de canciones con
  stems se mantienen.
- El consumo de tempo fraccionario puede volver a sincronizar sobre un beat
  ya renderizado: se reprodujo con 44,1 kHz, bloques de 257, ratio 0,75,
  source frame inicial 3737808, como ataques 256/257. La identidad del último
  pulso musical deja sólo el ataque 256. Se limpia en resets/seeks/Loop/Ir.
- La prueba nativa añade dos minutos a tempo reducido, el borde determinista
  anterior y cambios en vivo 126 -> 252 -> 94,5 BPM con la rampa existente de
  100 ms. Exige un ataque por beat, ausencia de deriva acumulada y continuidad
  de la identidad musical. La prueba de rampas midió 32 ataques y error máximo
  de 0,5 frames fuente.
- Los samples woodblock guardan un segundo de PCM, pero el último frame no
  cero de la negra está a 54,94 ms. La carga elimina sólo ceros finales,
  conservando un cero para la interpolación. El renderer abandona el recorrido
  de voces vacías al terminar la última, también con click sintético. Se prueba
  equivalencia de PCM estéreo remuestreado 44,1 -> 48 kHz y samples totalmente
  cero. Las cuatro capturas woodblock de referencia fueron **PCM bit a bit
  idéntico** antes/después de optimizar colas y voces.

Microbenchmark del motor, Linux x86_64, GCC 14, JUCE 8.0.12, Release,
`JUCE_USE_SIMD=0`: tres pasadas por versión, cada una de 20.000 callbacks de
256 frames a 44,1 kHz. Medianas de las medias por pasada:

| Caso | Antes (us/bloque) | Después (us/bloque) | Reducción |
| --- | ---: | ---: | ---: |
| Woodblock, negras, 120 BPM | 4,287 | 1,235 | 71,2 % |
| Woodblock, todas las subdivisiones, 300 BPM | 21,791 | 5,095 | 76,6 % |
| Sintético, todas las subdivisiones, 300 BPM | 3,115 | 3,012 | 3,3 % |

El p99 mediano del caso woodblock denso pasó de 74,923 a 14,112 us. Es una
medida del componente en esta máquina, no del callback completo, un dispositivo
móvil ni una garantía de deadline: el host compartido puede introducir pausas.

El benchmark `MTControlsMetronomeTests --benchmark` admite una auditoría Linux/
glibc mediante `tests/metronome_rt_audit.c`, que se compila como biblioteca de
prueba con `cc -O2 -shared -fPIC ... -ldl -pthread` y se carga con `LD_PRELOAD`.
Su autoprueba exige detectar malloc/free y mutex deliberados antes de medir.
Observó **cero malloc/calloc/realloc, free y pthread_mutex_lock** en el hilo de
render durante las pasadas. Sin ese preload el JSON indica `rt_audit:false`;
los ceros no acreditan auditoría. El probe no se enlaza ni distribuye con la app.
La inspección del hot path del clic no encontró I/O, creación de buffers,
publicación de generaciones ni locks; la carga/recorte sigue fuera del callback.

La orden repetida `setMetronomeEnabled(true)` también reprodujo ataques 256/257
en el borde de tempo: borraba innecesariamente la identidad del beat. Ahora el
setter es idempotente y el test repite el valor entre ambos callbacks. Un cambio
real publica reset antes de enabled mediante release/acquire.

Evidencia local adicional: `benchmark-summary-final.json`,
`benchmark-release-{before,after}-*.log`, `tempo-*-tests.log` y
`engine-optimized/` bajo `/workspace/scratch/click-investigation/`.

Gate final ampliado tras todas las correcciones: **12/12**, 67,40 s, con los
mismos targets anteriores recompilados y Bungee real. La cadencia de 120 s
midió 189 ataques, cero pares próximos y error máximo de un frame; la orden
repetida dejó sólo el ataque 256. También pasó `metronome_engine` en Release.
La auditoría es del motor del clic, no del callback completo: el harness nativo
renderiza PCM sin dispositivo host y conserva avisos/asserts Debug de dispositivo
no configurado y FileLogger presentes en sus logs. No acredita audio físico,
latencia de un driver ni todas las plataformas. Se mantiene la UI de `d1a0881`.

## Colapso de onda en canción en blanco (2026-10-15)

Base `9150e054` en `feat/blank-song`, trabajo sólo en nube. El editor reservaba
la altura de WaveformBar aunque el componente estuviera oculto. Ahora la canción
cargada con `blankSong` y sin stems reserva **cero** altura, incluyendo toolbar
extra y altura expandida. Mixer/setlist empiezan debajo de TopBar; Pads conserva
su drawer/popover y al salir del perfil teléfono recupera su visibilidad según
el estado abierto/cerrado, sin recrear sus componentes.

La transición actualiza inmediatamente el layout en carga local, snapshots
remotos (también el mismo ID al recibir stems), retorno de Cliente y descarga
local. No infiere el tipo por silencio, duración, bins vacíos ni errores de
lectura. La altura expandida anterior se conserva para la próxima canción con
audio. Agregar stems en la edición habitual retira el flag de blanco; la
transacción real guarda/reabre el mismo `.pwproj` e ID con audio incluido.
La tarjeta de creación aprobada y los motores de clic/pads/transporte de
`9150e054` no se modifican.

Verificación Linux Debug, JUCE 8.0.12, GCC 14, Bungee 2.4.24 real,
`JUCE_USE_SIMD=0`:

- `MTControls_Standalone` y targets de integración compilados. La nueva suite
  `blank_song_layout` ejecuta el Editor completo y su loader real, con sólo los
  transportes externos de autenticación sustituidos por las fixtures offline
  existentes; no sustituye audio ni Bungee.
- Se revisaron 18 snapshots nativos (audio/blanco/audio restaurado), con sus
  bounds en `build/blank-waveform-collapse-qa/geometry.json`. Perfil teléfono
  667x375: mixer 209 -> 317 puntos de alto; 844x390: 202 -> 326. Geometrías
  tablet 1024x768 y 768x1024: +166 puntos; escritorio 1280x900 y 1440x900:
  +192. Al volver a audio se recuperaron exactamente los bounds anteriores.
- La pasada con capturas pasó 153 comprobaciones; sin exportar imágenes, 134.
  Comprueba ocultación de selectores de onda/secciones, conservación de los
  controles disponibles en cada perfil, Pads al volver de teléfono, reloj en
  reproducción y revisión del transporte intactos durante resize, clic/pad/
  volumen/tono/tempo conservados, audio normal sin autoplay, onda todavía sin
  bins/en carga, referencia normal sin stems y cambio remoto con stems aun si
  llega un flag de blanco atrasado.
- `song_import_wizard` verifica la tarjeta original, convertir el borrador al
  agregar un WAV real, persistencia/reapertura con un stem y el mismo ID. La
  fixture usa FLAC real si no hay encoder instalado, para no descargar nada ni
  inventar un conversor. Esa ruta pasó; además se probó con FFmpeg 7.1.5 real
  del host, disponible sólo junto al ejecutable de test. Pasaron WAV -> FLAC
  y AAC/ALAC M4A -> FLAC -> proyecto -> Biblioteca.
- Gate principal **13/13**, 53,91 s: `local_demo_access`, `blank_song_layout`,
  `remote_session`, `remote_section_control`, `song_project_creation`,
  `song_import_wizard`, `blank_song_transport`, `metronome_transport`,
  `metronome_engine`, `audio_engine_core`, `pads_engine`, `bungee_processor`
  y `stem_mix_engine`. `section_automation` pasó aparte en 27,07 s: 14 suites
  dirigidas aprobadas en total. El clic mantiene 500 inicios concurrentes sin
  duplicados, 189 pulsos en 120 s sin pares próximos y sus rampas de tempo.
- `waveform_editing` se ejecutó aparte: conserva sus **dos fallos previos**,
  repaint con diferencia RGB de 1 y ancla flotante tras scroll horizontal;
  no aparecieron fallos nuevos. No se afirma una suite global completamente
  verde. Los avisos/asserts Debug de fonts/dispositivo/singletons del harness
  permanecen visibles en los logs.

El harness Linux invoca el mismo `layoutMainUi()` con perfil teléfono para
comprobar esa rama y renderiza componentes JUCE reales; las otras capturas son
geometrías de tablet/escritorio bajo Linux. No acredita safe-area, teclado,
giros, touch ni GPU de iOS/Android reales. No hubo audio/MIDI físico, builds
macOS/iOS/Android/Windows ni instalación en Mac. La nueva distribución no toca
el DSP; cargar/editar audio conserva sus transacciones existentes, no promete
mantener una reproducción activa durante el reemplazo de stems.

Logs locales: `/workspace/scratch/blank-collapsed-final-build.log`,
`blank-collapsed-layout-final.log`, `blank-collapse-final-tests.log`,
`blank-collapse-gate-output.log`, `blank-conversion-offline-test.log` y
`blank-collapse-waveform-regressions.log`. Capturas en
`build/blank-waveform-collapse-qa/`. Sin push, PR, Actions, publicación o deploy.
