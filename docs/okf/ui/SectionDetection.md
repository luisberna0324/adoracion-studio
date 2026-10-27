---
type: UI and Analysis Contract
title: Detección asistida de secciones
description: Propuestas locales revisables sobre el editor de arrange existente
resource: src/analysis/SectionDetectionService.h
tags: [audio, sections, macos, analysis, offline]
timestamp: 2026-10-27
---

# Detectar secciones

La detección prepara propuestas transitorias; no crea un segundo sistema de
secciones persistidas. Al aceptar se usa `SongArrangementPartSnapshot`, el
historial del `WaveformBar` y su callback habitual de persistencia. El análisis
no modifica originales, faders, BPM, compás, ventana de reproducción ni Play Cloud.

## Inspección del repositorio

- `ImportedSongData` contiene stems, BPM original, compás, `arrangement` y
  `tempoMap`. `ImportedStemData` contiene rutas, categoría, nombre, duración,
  frecuencia y canales; el modelo actual no guarda offsets por stem. El
  adaptador declara offset cero; el contrato de análisis admite offsets
  explícitos para un proveedor/importador futuro.
- `SongArrangementPartSnapshot` distingue tiempo de arrange de región de audio
  fuente mediante `sourcePartId`, `sourceStartSec` y `sourceEndSec`. Las copias,
  movimientos y recortes pueden hacer que ambos tiempos difieran. No aplicar
  propuestas de tiempo fuente directamente sobre un arreglo reordenado.
- La creación usa `SongImportWizardComponent` y la transacción compartida
  `SongProjectCreation::save`. El paso denominado Secciones en el asistente
  corresponde al mapeo de pistas/categorías; no es un editor del arreglo.
- Las secciones se guardan en el modelo existente de Biblioteca y en los ajustes
  del proyecto `.pwproj`, según el tipo de canción. Una canción sin anotaciones
  tiene `arrangement.parts` vacío, sin una sección sintética de canción completa.
- `WaveformBar::setArrangement` representa una carga y borra el historial.
  No usarlo para confirmar detección. La confirmación usa
  `applyDetectedSections`, `pushArrangementUndoSnapshot` y
  `notifyArrangementChanged(true)`. El callback configurado en `PluginEditor`
  llama `SongLibraryStore::updateSongArrangement` y
  `MTControlsAudioProcessor::setSongArrangement`.
- El scheduler y el motor consumen las secciones existentes para seek, saltos,
  loops y regiones fuente. La detección no modifica su implementación. El
  scheduler admite hasta 256 secciones.
- CMake compila C++17 con JUCE y utiliza Objective-C++ en Apple. Los directorios
  locales `build/codex-modal-macos` y `build/macos-release` usan Ninja, arm64 y
  deployment target macOS 12.0. El target iOS observado es 18.5. No elevarlos
  para habilitar un proveedor opcional.

Véanse [creación](../importing/SongCreation.md),
[persistencia](../importing/LibraryPersistence.md),
[scheduler](../engine/SectionScheduler.md) y
[automatización](SectionAutomation.md).

## Dominio y ejecución

`SectionDetection.h` define el contrato versionado: huella de audio, proveedor,
versión del algoritmo, duración, estructura cruda, pulsos/compases, cues de guía
y sugerencias. Los tiempos son `double` en segundos de la fuente real; la
conversión a muestras utiliza su frecuencia original. El índice interno de
compás comienza en cero y el índice visual es ese valor más uno.

`SectionDetectionService::start` inicia un worker cancelable; ni preparación ni
análisis se ejecutan en el callback de audio. Progreso y completion se entregan
desde el worker: la UI debe reenviarlos al message thread con `callAsync` y
`SafePointer`, comprobar identidad de canción y descartar resultados obsoletos.
El handle `SectionDetectionTask` permite cancelar sin esperar en la UI.

Las fases son preparando audio, analizando estructura/compases, transcribiendo
guía, fusionando sugerencias, revisión, error y cancelado. Una API sin porcentaje
real requiere progreso indeterminado. La fase de transcripción está reservada
para el proveedor de voz; su presencia en el enum no acredita ASR implementado.

La clasificación musical de stems se confirma por selección del usuario. Las
categorías existentes ayudan a presentar roles, pero el nombre de archivo por
sí solo no debe decidir la exclusión de guía o click. La mezcla temporal usa
solo pistas musicales seleccionadas; conserva silencios, duración completa y
offsets explícitos sin cambiar archivos o controles de mezcla. Fuentes con
distintas frecuencias se proyectan por tiempo absoluto, sin sumar redondeos de
conversión entre bloques. El análisis guarda únicamente resultados y metadatos
mínimos en caché; sus claves incluyen contenido, proveedor, algoritmo y parámetros.
Los temporales de audio pertenecen al trabajo y se eliminan al finalizar.

## Proveedor Apple opcional

La frontera C `AppleMusicStructureBridge.h` intercambia rutas locales y JSON,
sin tipos JUCE, Swift ni CMTime en la ABI. `AppleMusicStructure.swift` usa
`#if canImport(MusicUnderstanding)` y comprobación runtime. En plataformas sin
bridge, la implementación C++ informa indisponibilidad y el editor manual
permanece operativo. iOS no se anuncia compatible sin build y validación propios.

El proveedor solicita únicamente `.rhythm` y `.structure`; crea un `AVURLAsset`
con duración y tiempos precisos y convierte los rangos/ticks a segundos. Apple
aporta límites y pulsos, no nombres de Verso/Coro ni una probabilidad de sección.
La confianza de una propuesta es una heurística de PlayWorship y su razón debe
ser visible. Sin una guía reconocida, conservar nombres de sección sugerida o
desconocida.

El entorno inspeccionado contiene Xcode 26.6, SDK macOS 26.5 y macOS 26.2.
No contiene MusicUnderstanding. Compilar el fallback o ejecutar fixtures
inyectados no verifica la rama nativa; requiere además SDK y sistema compatibles.
La transcripción con SpeechAnalyzer/SpeechTranscriber y la evaluación de la guía
adjunta pertenecen a la entrega 2 y no se acreditan mediante normalización de
texto sintetizado.

La documentación oficial declara MusicUnderstanding para macOS/iOS 27 o
posterior y el ejemplo de Apple requiere Xcode 27. El bridge actual solo registra
la ruta macOS; mantener la exclusión runtime aunque se compile con un SDK nuevo.

Referencias: [Music Understanding](https://developer.apple.com/documentation/musicunderstanding),
[MusicUnderstandingSession](https://developer.apple.com/documentation/musicunderstanding/musicunderstandingsession),
[StructureResult](https://developer.apple.com/documentation/musicunderstanding/structureresult),
[SpeechAnalyzer](https://developer.apple.com/documentation/speech/speechanalyzer).

## Revisión en la línea de tiempo

`WaveformBar` expone una sola acción `Detectar secciones` en Más. Su disponibilidad
y explicación se reciben del orquestador mediante
`setSectionDetectionAvailability`; el callback se registra con
`setOnSectionDetectionRequest`. Si no hay proveedor compatible se presenta
indisponibilidad condicionada, sin una acción vacía.

`setSectionDetectionPreview` muestra bloques translúcidos con límites punteados
y rótulo Sugerencia sobre la onda real. Es una capa visual separada del snapshot
comprometido: no altera reproducción, persistencia ni historial.
`clearSectionDetectionPreview` descarta esa capa. Cargar otra canción también
la elimina. La edición, selección y escucha de propuestas pertenecen al panel
de revisión y utilizan estos bloques como contexto temporal.

`applyDetectedSections` recibe el snapshot final ya fusionado. Comprueba que
cada sección y cue existentes mantengan ID, nombre, región fuente, acciones y
automatización. Rechaza reemplazos, IDs duplicados, propuestas fuera de la fuente,
solapamientos nuevos y superar el límite del scheduler. Reaplicar un snapshot
sin adiciones no genera cambios ni historial. Al aceptar añade una entrada de
undo y llama una sola vez la ruta normal del editor; deshacer y rehacer recorren
la misma persistencia. Durante un borrador de automatización o carga se rechaza
la confirmación. El orquestador debe además verificar que el snapshot de partida
y la identidad de canción sigan vigentes.

## Prueba manual reproducible

1. En una Mac con proveedor disponible, abrir una canción con stems locales.
   Registrar sistema, SDK y build usados. Conservar copia del proyecto para
   comparar secciones/BPM/compás y no usar una canción privada como fixture de
   persistencia automatizada.
2. Abrir Más → Detectar secciones, confirmar roles música/guía/click y ejecutar.
   Durante preparación/análisis comprobar que la ventana responde; reproducir,
   hacer seek, cambiar mute/solo y observar waveforms. No interpretar un build
   correcto como una comprobación física de ausencia de cortes de audio.
3. Revisar los bloques provisionales. Escuchar cada límite, moverlo a un compás
   cuando exista una cuadrícula confirmada, renombrar, añadir/eliminar y seleccionar
   un subconjunto. Verificar que el nombre desconocido no se convierte en Coro
   por energía o por un resultado de Apple sin evidencia de guía.
4. Descartar: ninguna sección, cue, automatización ni ajuste debe cambiar.
   Repetir, cancelar en cada fase y comprobar ausencia de temporales al concluir.
5. Repetir con una sección manual corregida y automatizada que ocupe parte del
   audio. Aceptar propuestas libres de conflicto: esa sección debe conservar
   todos sus valores. Deshacer una vez restaura el snapshot previo completo;
   rehacer restaura las nuevas anotaciones.
6. Analizar de nuevo: no duplicar lo confirmado. Cambiar de canción durante el
   worker: el resultado anterior no debe aparecer ni guardarse sobre la nueva.
7. Guardar y reabrir el proyecto mediante el flujo existente. Comprobar límites,
   nombres, salto de sección, loop, tempo/pitch, cues y sincronización, registrando
   por separado build, tests, escucha y persistencia observada.
8. En SDK/sistema sin soporte, abrir el mismo editor y comprobar explicación de
   indisponibilidad y funcionamiento del editor manual. No declarar análisis
   nativo verificado en este caso.

La guía de referencia se copia explícitamente desde el adjunto a un fixture
local ignorado por Git; véase `tests/fixtures/section-detection/README.md`.
Su secuencia esperada sirve para contrastar una futura transcripción real, no
para introducir nombres o tiempos en el algoritmo. Medir diferencia entre cue
hablado e inicio musical requiere los stems/click correspondientes y un etiquetado
manual; disponer solo de `Guia.mp3` no acredita esa alineación.

## Validación automatizada

`waveform_editing` ejercita la capa de preview sobre el componente real, descarte
sin callback, aceptación por la ruta habitual, conservación de sección manual
con acciones/automatización/cue, rechazo atómico de conflictos, repetición sin
duplicados y undo/redo en una operación. Comprueba además la primera detección
sobre un arreglo vacío. Las suites del dominio y servicio utilizan proveedores
inyectados y audio sintético; no sustituyen ejecución nativa Apple ni ASR.

Comandos locales de referencia:

```sh
cmake --build build/codex-modal-macos --target MTControlsWaveformEditingTests --parallel 3
ctest --test-dir build/codex-modal-macos -R '^waveform_editing$' --output-on-failure
cmake --build build/codex-modal-macos --target MTControls_Standalone --parallel 3
git diff --check
```

Registrar los resultados efectivos de esos comandos en la entrega. Los datos de
prueba persistentes usan el wrapper de aislamiento existente; ver
[Testing](../Testing.md). Un build macOS no verifica builds Windows, Android o iOS.

## Compilación portable observada el 27 de octubre de 2026

La biblioteca `MTControls` se compiló y archivó correctamente para iOS Simulator
arm64 con SDK 26.5, incluyendo el fallback C++ y las unidades nuevas de análisis
y revisión. Se ejecutaron incrementales finales para incluir los cambios
de `WaveformBar`, `RemoteEditorIntegration`, las cadenas UTF-8 y el texto visible
del panel; todos terminaron con `BUILD SUCCEEDED`. Artefacto:
`build/ios-simulator-ipad-mini/Debug-iphonesimulator/libMTControls.a`.
Esta prueba no enlaza la app iOS final ni ejecuta MusicUnderstanding.

```sh
cmake -S . -B build/ios-simulator-ipad-mini -DFETCHCONTENT_UPDATES_DISCONNECTED=ON
xcodebuild -project build/ios-simulator-ipad-mini/MTControls.xcodeproj \
  -target MTControls -configuration Debug -sdk iphonesimulator -jobs 3 \
  CODE_SIGNING_ALLOWED=NO build
```

Android arm64-v8a se compiló y enlazó con NDK 28.1.13356709 y API 29, usando
Gradle offline y el directorio CMake generado. Se limitó la compilación nativa
a tres trabajos, se repitió incrementalmente y el target Gradle final terminó
con `BUILD SUCCESSFUL`. Artefacto:
`android/app/build/intermediates/cxx/Debug/5fh4p553/obj/arm64-v8a/libjuce_jni.so`.
Los nuevos archivos C++ entraron mediante el glob existente; no se compiló Swift.

```sh
cd android
./gradlew ':app:configureCMakeDebug[arm64-v8a]' --offline --max-workers=3
cmake --build app/.cxx/Debug/5fh4p553/arm64-v8a --target playworship --parallel 3
./gradlew :app:externalNativeBuildDebug --offline --max-workers=3
```

El hash del directorio `.cxx` depende de la configuración local; consultar el
directorio generado por Gradle antes de reutilizar el segundo comando.
Logs de esta ejecución:
`/tmp/playworship-sections-ios-build.log` y
`/tmp/playworship-sections-android-build.log`.
No se generó un APK completo, instaló ni abrió una app en estas pruebas.
Windows no se compiló: no se encontró toolchain clang-cl/MinGW en el entorno.

## Entrega local y archivos modificados

Los cambios de esta entrega permanecen en la rama de trabajo, sin merge,
publicación ni instalación. Se conservaron los cambios locales anteriores.

| Área | Archivos de esta entrega |
| --- | --- |
| Dominio y ejecución | `src/analysis/SectionDetection.{h,cpp}`, `SectionDetectionService.{h,cpp}` |
| Apple y fallback | `src/platform/AppleMusicStructure.swift`, `AppleMusicStructureBridge.{h,cpp}`, `cmake/SectionDetection.cmake` |
| Revisión | `src/ui/panels/SectionDetectionPanel.{h,cpp}`, `WaveformBar.{h,cpp}` |
| Conexión al editor | `src/PluginEditor.{h,cpp}`, `src/RemoteEditorIntegration.cpp` |
| Compilación y pruebas | `CMakeLists.txt`, `tests/section_detection_tests.cpp`, `section_detection_service_tests.cpp`, `section_detection_panel_tests.cpp`, `waveform_editing_tests.cpp` |
| Fixture y contratos | `tests/fixtures/section-detection/README.md`, este documento, `docs/okf/audio-systems/SectionDetection.md`, índices OKF y `docs/okf/Testing.md` |

La app macOS `MTControls_Standalone` y los tres targets nuevos de prueba
compilaron con el puente Swift de indisponibilidad. El ejecutable conserva
`minos 12.0` y SDK 26.5, verificado con `otool`. El helper de compilación del
puente se comprobó también de forma aislada con Ninja y Xcode universales
arm64/x86_64, y con la opción del puente apagada. Estos checks no compilan la
rama `canImport(MusicUnderstanding)` de SDK 27.

```sh
cmake -S . -B build/codex-modal-macos -DMTCONTROLS_BUILD_TESTS=ON
cmake --build build/codex-modal-macos --target \
  MTControlsSectionDomainTests MTControlsSectionServiceTests \
  MTControlsSectionPanelTests MTControlsWaveformEditingTests \
  MTControlsSectionAutomationTests MTControlsAudioEngineCoreTests \
  MTControlsStemMixEngineTests MTControlsMetronomeTests \
  MTControlsSongMixExportTimelineTests MTControls_Standalone --parallel 5
ctest --test-dir build/codex-modal-macos --output-on-failure \
  -R '^(section_detection(_service|_panel)?|waveform_editing|section_automation|audio_engine_core|stem_mix_engine|metronome_engine|song_mix_export_timeline)$'
git diff --check
```

Resultado de CTest: **9/9 suites aprobadas**, 26,05 s. El dominio informó
82 comprobaciones. La suite del servicio verifica una mezcla PCM real con
proveedor sintético, offsets, distintas frecuencias, silencios, exclusión de
guía/click, amplitudes flotantes mayores de 1 sin clipping intermedio, errores,
cancelación, huella de contenido y eliminación de temporales. La suite del
panel pulsa los controles reales y verifica revisión, edición, ajuste a
compases, escucha por callback, selección, añadido/borrado, conflictos,
confirmación parcial, cancelación e indisponibilidad.

La captura `output/section-detection-preview.png` muestra el componente de
revisión real con un proveedor inyectado de pruebas; no representa un análisis
Apple de la canción adjunta. Los tests de waveform comprueban además preview
sin notificación ni historial, aceptación por la transacción existente y
undo/redo con secciones manuales, cues y automatización conservados.

Logs macOS: `/tmp/playworship-section-build-final.log`,
`/tmp/playworship-section-build-final-incremental.log` y
`/tmp/playworship-section-ctest.log`.

Falta compilar/ejecutar la rama nativa con SDK/macOS 27, comprobar sus modelos y
funcionamiento sin red, incorporar ASR local temporizado y comparar con los
stems/click de referencia. Tampoco se verificaron aquí escucha física continua,
QA manual de la app completa, build Windows, app iOS final ni APK completo.
La comparación de click y las equivalencias por análisis de stems siguen siendo
extensiones posteriores; no se deducen nombres musicales a partir de energía.
