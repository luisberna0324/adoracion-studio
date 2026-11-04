---
type: Analysis Contract
title: Assisted Section Detection
description: Propuestas locales revisables sin reemplazar el arreglo existente
resource: src/analysis/SectionDetectionService.h
timestamp: 2026-11-03T00:00:00-03:00
---

# Detectar secciones

La detección es una operación offline del editor, fuera del callback de audio.
No crea un segundo arreglo persistente: devuelve un borrador de propuestas.
El editor aplica las seleccionadas mediante su transacción de arreglo existente,
su historial de deshacer y la persistencia habitual de ajustes. Analizar o
descartar no guarda secciones, cambia faders, BPM, métrica ni archivos fuente.

## Modelo real y coordenadas

`ImportedSongData` contiene `ImportedStemData`, `SongArrangementSnapshot` y
`SongTempoMapSnapshot`. Las secciones persistentes son
`SongArrangementPartSnapshot`; sus automatizaciones, acciones de reproducción,
referencias de audio y cues anclados se conservan. No hay un campo de procedencia
automática/manual ni de bloqueo en este modelo: **todas las secciones existentes
se consideran protegidas**. Las propuestas solo pueden rellenar intervalos libres.
No se deduce que una sección completa de canción sea descartable.

El modelo de stem actual no expone offsets. El adaptador entrega explícitamente
`offsetSeconds = 0`; `AnalysisStem` sí permite un offset para fuentes futuras y
pruebas. El análisis usa el tiempo de la fuente sincronizada, sin aplicar el
ratio de tempo ni el pitch de reproducción. No se acepta su inserción en un
arreglo con copias o con un rango fuente explícito que difiera de las coordenadas
de arreglo. No se puede proyectar implícitamente una sugerencia de la fuente
sobre audio reordenado.

`secondsToSourceSamples` redondea una única vez con la frecuencia fuente;
`sourceSampleToTimelineSeconds` calcula `sample / sampleRate + offsetSeconds`.
Los tiempos persistidos y los del contrato son `double`. Los índices de compás
internos parten de **0**; la presentación humana suma 1. Una cuadrícula es una
lista de tiempos crecientes y no implica 4/4, tempo constante ni primer pulso en
cero. La casilla de sincronización requiere confirmación explícita antes de
usar el mapa de tempo del proyecto para cuantizar. El análisis no certifica esa
sincronización ni corrige el mapa.

## Servicio y preparación de audio

`SectionDetectionService::start` captura una petición por valor, ejecuta el
trabajo en segundo plano y devuelve `SectionDetectionTask` cancelable. Los
callbacks de estado y resultado pertenecen al worker: la UI usa `callAsync` y
`SafePointer`. Cancelar no espera ni bloquea la UI. El callback de audio, el
scheduler y los stems del reproductor no participan en esta operación.

`AnalysisStemRole` distingue música, guía, click y excluido. La UI inicializa los
roles a partir de la categoría ya guardada y permite corregirlos antes de
analizar; no decide exclusivamente por nombres de archivo. Guía y click aportan
duración de timeline, pero no muestras a la mezcla musical. No se exige que
existan. Una selección sin música informa un error.

La mezcla temporal usa la frecuencia máxima de los stems musicales, canales
promediados y posiciones absolutas `timelineSeconds * sourceRate`, con
interpolación lineal cuando difieren las frecuencias. No acumula desplazamientos
por conversiones sucesivas. Mantiene los silencios, el offset explícito y la
cola del stem incluido más largo; no recorta al primer sonido. Dos pasadas
permiten escribir primero PCM float y después una mezcla PCM24 con pico máximo
0,95. No usa volumen, mute, solo, buses ni procesamiento de reproducción.
Los originales permanecen abiertos solo para lectura. Los dos WAV se eliminan
al terminar, fallar o cancelar, una vez que el proveedor deja de leerlos.

El MVP de estructura limita el análisis local a 12 horas y 384 kHz; los errores de decodificación,
escritura, datos no finitos y ausencia de música vuelven como error visible.
La mezcla lineal es una herramienta de análisis, no un render para distribuir.

## Transcripción local de guía: primer incremento

Esta ruta es independiente del proveedor Apple y de la mezcla musical anterior.
`GuideTranscription.h` define `IGuideTranscriber`, `GuideTranscriptionRequest`,
`GuideTranscript` y segmentos con texto e inicio/fin en segundos. Son tiempos
del **anuncio hablado**, en coordenadas de audio fuente más `offsetSeconds`;
no son inicios musicales, compases ni límites de sección. El resultado conserva
proveedor/versión, huellas de audio y modelo, idioma y duración.

Una petición con `transcribeGuideOnly=true`, `guideModelFile` y
`guideLanguage` (español `es` inicialmente) exige exactamente un stem confirmado
como Guía. El servicio lee esa pista; no abre ni decodifica las pistas musicales,
click o excluidas y no exige que existan. `guideAvailable()` y
`guideUnavailableReason()` consultan el backend de voz, sin depender de
`available()` del analizador de estructura. Disponer del backend compilado no
certifica que el archivo de modelo elegido sea legible o válido.

`SectionDetectionService::start` conserva su worker y cancelación; admite un
callback opcional de progreso numérico, entregado desde ese worker. El outcome
incluye `guideTranscript`. Los textos reconocidos también pueden pasar por
`normalizeGuideCue` para producir cues léxicos, conservando los tiempos hablados.
**`result.suggestions` queda vacío:** esta ruta no fusiona ni cuantiza, no crea
secciones y no llama la transacción de aceptación del arreglo. Analizar una guía
sola no acredita alineación con música/click ni porcentaje de precisión.

El backend local usa **whisper.cpp v1.9.5**. Se habilita por CMake en escritorio;
en iOS/Android permanece deshabilitado por defecto hasta sus builds y QA propios.
La inferencia CPU se serializa y usa hasta cuatro hilos; prepara audio en ventanas
de 20 s sin retener el PCM completo de una canción. Conserva límites de entrada
de 12 horas y 384 kHz y limita inicialmente el archivo de modelo a 256 MiB.
La lectura, hash, preparación e inferencia están fuera del callback de audio.

La caché de esta ruta vive en `guide-transcripts-v1/` dentro del directorio de
análisis. La clave incluye contenido completo de audio/modelo, offset, idioma,
proveedor y versión. Guarda JSON de transcripción/metadatos, nunca PCM ni audio
enviado a una red; no se incorpora a Biblioteca o Play Cloud. Se comprueban de
nuevo identidad y rangos antes de reutilizarla. Cancelar o cambiar de canción
invalida la entrega al panel; no confirma secciones.

### Preparar el modelo una vez

En el flujo nativo `Sugerir secciones`, si falta el modelo local se ofrece
`Descargar y continuar`. Esa acción explícita prepara el modelo una vez mediante
`GuideModelProvisioner`, con progreso y cancelación en un worker; no bloquea el
hilo de interfaz. La descarga es únicamente un GET del asset fijado: no lee ni
sube la guía, los stems o el proyecto. Después se puede transcribir sin red.
También se puede elegir un modelo local compatible o prepararlo desde el
repositorio con el script auxiliar:

```sh
python3 scripts/prepare_whisper_guide_model.py
python3 scripts/prepare_whisper_guide_model.py --verify-only
# Destino alternativo completo; luego elegirlo en Modelo de voz…
python3 scripts/prepare_whisper_guide_model.py --destination /ruta/ggml-base-q5_1.bin
```

La preparación nativa y el script usan el mismo modelo multilingüe
`ggml-base-q5_1.bin`, 59 707 625 bytes (aprox. 57 MiB). El script auxiliar usa
solo la biblioteca estándar de Python 3.10+.
El destino por defecto coincide con
`defaultWhisperGuideModelFile()`: `userApplicationDataDirectory/MTControls/models/`;
en macOS es `~/Library/MTControls/models/ggml-base-q5_1.bin` (no Application
Support). `--destination` admite otra ruta de archivo. `--verify-only` no usa red.
Un modelo correcto existente evita la descarga; uno inválido se conserva y se
solicita moverlo o elegir otro destino.

El modelo está fijado al commit HF
`f281eb45af861ab5e5297d23694b7d46e090c02c` y SHA-256
`422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898`.
Ambas rutas comprueban tamaño y hash antes del reemplazo atómico y eliminan su
temporal ante error o cancelación normal. El worker nativo termina el read de
red acotado en curso antes de eliminar el temporal; pedir cancelación no espera
en la UI. Ninguna ruta sustituye un archivo existente inválido. Preparar el
modelo no implica que se haya validado el reconocimiento de una guía concreta.

Fuentes del pin: [modelos oficiales de whisper.cpp v1.9.5](https://github.com/ggml-org/whisper.cpp/blob/v1.9.5/models/README.md)
y [puntero Git LFS del modelo fijado](https://huggingface.co/ggerganov/whisper.cpp/raw/f281eb45af861ab5e5297d23694b7d46e090c02c/ggml-base-q5_1.bin).

## Borrador visual de entradas desde la guía

`GuideSectionAlignment.h` separa la transcripción de su revisión musical.
`alignGuideSections(transcript, options)` devuelve `GuideAlignmentDraft`, con
cuadrícula y propuestas transitorias; no modifica `ImportedSongData`, BPM,
mapa de tempo ni el arreglo confirmado. El caller propone inicialmente el
inicio del primer fragmento hablado como `originSeconds`, incluso si ese
fragmento es un conteo. Ese origen es el pulso 1 del compás 1 y puede corregirse.
Cambiarlo recalcula el borrador completo a partir de los tiempos de voz originales.

La cuadrícula usa BPM original y `SongTempoMapSnapshot` en segundos fuente.
Cada pulso dura `(60 / BPM) * 4 / denominador` y cada compás contiene el
numerador de pulsos. Cambiar BPM conserva la fase fraccionaria del pulso y del
compás; cambiar métrica inicia un compás nuevo, como `PracticeTimelineRuler`.
Los huecos del mapa usan el ritmo base. La enumeración no depende del zoom ni
de una cantidad de píxeles: incluye todos los pulsos y compases desde el origen
hasta la duración. Admite hasta 250000 marcas; exceder el límite devuelve un
error y elimina la cuadrícula parcial. Un mapa inválido, desordenado o
superpuesto se rechaza. No se sustituyen metadatos inválidos por tempo supuesto.

`anticipationBars` admite 0, 1 y 2, con 1 como propuesta inicial. La entrada se
ubica en el compás que contiene el anuncio más esa cantidad de compases. Esto
es una elección revisable, no una medición de la anticipación real de la voz.
Un destino fuera del rango se limita a un compás que deje al menos 0,25 s de
sección y queda señalado con confianza baja. El inicio hablado original,
desplazamiento y anticipación resultante permanecen en la propuesta.

Los nombres reconocidos usan `normalizeGuideCue`, incluidos sus ordinales.
Un texto desconocido se conserva como `Revisar: <texto>` para poder corregirlo;
no se descarta por un error de reconocimiento. Los conteos puros se excluyen
como secciones. No se deducen Intro o Final de silencios o de la posición.
Anuncios iguales que llegan a una entrada se fusionan; anuncios diferentes
o límites separados por menos de 0,25 s producen un bloque de revisión que
conserva todos los textos y su explicación, con confianza baja. Ninguna entrada
de esta ruta recibe automáticamente confianza alta. Los IDs dependen de
proveedor/versiones, huellas y fragmento hablado, sin depender del origen elegido.

Cada propuesta termina en el siguiente inicio; la última termina en la duración
fuente. `moveGuideSectionBoundary` cambia un inicio por ID y el final de su
vecina anterior, mantiene los otros inicios, el final de la canción y la
cuadrícula, y respeta el mínimo de 0,25 s. Puede ajustar el gesto al compás
válido más próximo; en empate elige el posterior. El movimiento marca ambas
vecinas afectadas como editadas, sin reemplazar el tiempo hablado original.
La aplicación al arreglo sigue siendo una transacción explícita del editor.

Las pruebas de dominio añadidas usan transcripciones sintéticas y comprueban
origen desplazado, 4/4 y 6/8, fase en cambios de BPM/métrica, cuadrículas largas,
conteos y texto desconocido, anticipación, colisiones y edición de vecinos.
Su existencia no acredita aquí una nueva ejecución de ASR, precisión musical
ni una validación táctil o auditiva de la aplicación.

## Proveedor Apple

`IMusicStructureAnalyzer` recibe un archivo local, duración, comprobación de
cancelación y devuelve `MusicStructureAnalysis` en segundos. Los tipos Swift y
`CMTime` no cruzan el dominio C++. `AppleMusicStructureBridge` usa una ABI C y un
callback único también tras cancelación; conserva contexto y temporales hasta
recibirlo.

El archivo Swift está protegido con `#if canImport(MusicUnderstanding)` y
disponibilidad de macOS 27. Solo solicita `.rhythm` y `.structure` y construye
`AVURLAsset` con `AVURLAssetPreferPreciseDurationAndTimingKey: true`. Devuelve
rangos de `sections`, `segments`, `phrases` y tiempos de `beats`, `bars`, BPM
opcional. No inventa nombres de secciones ni probabilidades de Apple.

**Limitación de esta entrega:** el entorno de desarrollo observado tiene macOS
26.2, Xcode 26.6 y SDK macOS 26.5, sin `MusicUnderstanding`. Se puede compilar y
ejecutar la rama Swift de indisponibilidad; la rama nativa de macOS 27 requiere
compilación con el SDK que incluya el framework y validación en un Mac compatible.
No se eleva el deployment target general de macOS 12. El SDK de iOS disponible
tampoco valida este proveedor: el registro nativo iOS permanece pendiente.
Windows, Android y builds sin Swift reciben el proveedor de indisponibilidad y
conservan la edición manual.

`cmake/SectionDetection.cmake` agrega el puente Swift solo en macOS. La opción
`MTCONTROLS_ENABLE_APPLE_MUSIC_STRUCTURE=OFF` permite compilar la ruta portable.
Compila el puente en un archivo estático por arquitectura y los une con `lipo`,
tanto en Ninja universal como en Xcode. No activa Swift como lenguaje de los
targets JUCE: conserva el driver C++ y sus flags. El target explícito de Swift
mantiene el deployment target existente. Se enlaza débilmente MusicUnderstanding
solo cuando existe en el SDK. Con un SDK anterior se compila la rama de
indisponibilidad, también en builds universales.

Referencias oficiales: [Music Understanding](https://developer.apple.com/documentation/musicunderstanding),
[MusicUnderstandingSession](https://developer.apple.com/documentation/musicunderstanding/musicunderstandingsession),
[StructureResult](https://developer.apple.com/documentation/musicunderstanding/structureresult).

## Contrato normalizado y fusión

`SectionDetectionResult`, esquema 1 y algoritmo `sections-mvp-1`, conserva
identidad de canción, duración, proveedor/versiones y huella del audio. Contiene
la evidencia original, cues de guía y propuestas. Cada propuesta conserva ID,
tipo, nombre, repetición, inicio original, inicio/fin revisables, índices de
compás opcionales, desplazamiento de cuantización con signo, fuentes, confianza
heurística, explicación, estado y bandera de edición. Los campos de guía
incluyen tiempo hablado y anticipación individuales cuando existe una asociación.

La fusión valida tiempos finitos, límites dentro de la duración, orden y ausencia
de superposición dentro de cada familia de rangos, pulsos y compases crecientes.
No convierte segmentos o frases en secciones si faltan secciones. Un resultado
sin secciones informa error. Los límites estructurales generan intervalos
contiguos dentro de `[0, duración]`; prefijos, huecos y colas sin nombre quedan
como `Sección sugerida`, nunca se clasifican como Intro o Final por su posición.

La cuantización busca el compás más próximo de la cuadrícula confirmada, o de la
evidencia del proveedor si no existe aquella. Solo ajusta si la distancia no
excede **0,35 s y un octavo del compás local**. Conserva el inicio crudo y el
ajuste, y no modifica la cuadrícula ni BPM. Los límites estructurales sin guía
permanecen sin clase musical aunque existan repeticiones.

`normalizeGuideCue` es una función de dominio disponible para la entrega de voz:
normaliza intro, verso/estrofa, pre-coro/precoro, coro, puente,
instrumental/solo/interludio, salida/outro y final, además de ordinales numéricos
y españoles. `salida final` se interpreta como Salida y se conserva literalmente
en `rawText`; `final` es una entidad distinta. Un conteo sin nombre musical no
genera una sección. No es un transcriptor ni un clasificador de música.

La fusión preparada para cues busca candidatos estructurales con anticipación
de hasta dos compases **locales**, sin un desplazamiento global fijo. Sin
cuadrícula usa una ventana conservadora de 1,5 s. Una coincidencia ambigua o
varios anuncios en conflicto no eligen un nombre. Los nombres repetidos reciben
ocurrencias separadas. Alta/media/baja son valoraciones propias y no porcentajes
de Apple ni una evaluación de precisión ASR.

La fusión no reemplaza un borrador que ya contenga propuestas editadas,
confirmadas o rechazadas: requiere descartar explícitamente ese borrador. IDs
estables basados en canción, huella e inicio original impiden acumular resultados
idénticos. La aceptación además comprueba solapamientos e IDs contra el arreglo
actual, no solamente contra el que existía al iniciar el análisis. Ante cualquier
conflicto no aplica ninguna seleccionada, no recorta límites y conserva todo el
estado previo. Respeta la duración mínima del editor de **0,25 s** y el máximo
del scheduler de **256** secciones.

## Caché y privacidad

La huella se deriva del contenido completo de los stems musicales, sus offsets,
duración, frecuencia y parámetros de preparación. La clave incluye esquema,
algoritmo/versiones, proveedor, huella, duración y parámetros de cuadrícula y
cuantización. El servicio guarda únicamente evidencia normalizada de análisis,
sin PCM ni transcripción de red, en `MTControls/cache/section-detection/`. Cada
lectura vuelve a validar el esquema y los rangos; cada solicitud vuelve a fusionar
con la cuadrícula vigente. Corrupción o incompatibilidad provoca recálculo.
El servicio limita archivos de lectura a 8 MiB y conserva hasta 64 entradas.
La cancelación y la revisión no guardan secciones en Biblioteca ni Play Cloud.

## Validación y siguientes entregas

`section_detection` prueba normalización, repetición, anticipación por cue,
cuadrículas variables y con primer pulso desplazado, 3/4 y 6/8, falta de guía,
silencio inicial/cola, rangos inválidos, preservación de automatizaciones y cues,
aceptación atómica, capacidad y duración mínima, cancelación, codec y caché.
`section_detection_service` usa un proveedor sintético para verificar el servicio,
mezcla, roles, offsets, frecuencias, errores y cancelación sin afirmar resultados
de MusicUnderstanding. La prueba del editor ejercita la transacción real.

```sh
cmake --build build/codex-modal-macos --target MTControlsSectionDomainTests MTControlsSectionServiceTests MTControlsWaveformEditingTests -j6
ctest --test-dir build/codex-modal-macos -R 'section_detection|waveform_editing' --output-on-failure
```

La guía adjunta de aproximadamente 04:46 vive fuera del repositorio. Su uso como
fixture debe ser explícito; no se distribuye audio privado. Los conteos de prueba
del normalizador verifican **textos de referencia**, no una transcripción real.
La nueva ruta Whisper debe registrar por separado su compilación, pruebas de
contrato, transcripción real y comportamiento sin red; las pruebas históricas
del MVP no acreditan esa inferencia. Sigue pendiente comparar sus tiempos
hablados con stems/click y límites musicales del mismo proyecto. No se declara
precisión ni alineación musical observada antes de esa medición.

La revisión visual posterior se verificó mediante cuatro suites aprobadas y
gestos conectados al cálculo real de `GuideSectionAlignment`. La descarga nativa
del modelo se ejecutó en una ruta QA aislada, con integridad verificada y reuso
posterior bajo bloqueo de red. Ver la evidencia de revisión sobre la onda en
[el contrato de UI](../ui/SectionDetection.md#evidencia-de-revisión-sobre-la-onda--4-de-noviembre-de-2026).

La entrega de stems puede añadir evidencia RMS, transitorios, actividad y
similitud mediante un proveedor separado que produzca evidencia en segundos.
El contrato actual separa fuente, estructura, tempo y cues para hacerlo sin
acoplar análisis al motor. No se han añadido detectores por stem ni librerías
de inferencia para esos detectores. Mayor energía no se convierte en Coro ni ausencia de instrumentos
en Verso.

## Evidencia del incremento Whisper — 3 de noviembre de 2026

- Worktree aislado en `feat/assisted-section-detection`, partiendo de `bfc12d6`;
  no se mezcló el snapshot histórico con la rama de FreeShow/Holyrics.
- Build macOS arm64 Release de la aplicación completa, dominio, servicio,
  panel, waveform y probe real, con Whisper activado y Apple desactivado.
  El binario conserva `minos 12.0` y SDK 26.5. Se incluye la licencia MIT de
  Whisper/ggml en `Resources/ThirdPartyLicenses`.
- El modelo multilingüe fijado se preparó y volvió a verificar offline mediante
  el script. La inferencia real se ejecutó bajo `sandbox-exec` con
  `(deny network*)`: `whisper-real-guide-v2.json` contiene 15 fragmentos de la
  guía de aproximadamente 04:46, con sus tiempos de voz. La salida no crea
  secciones ni certifica comienzos musicales.
- La primera pasada tenía el prompt deshabilitado por `n_max_text_ctx=0`.
  La versión final `guide-energy-sinc16k-beam5-prompt128-2` habilita contexto de
  vocabulario genérico, beam search 5 y normalización de la ventana de voz.
  Todavía se observan dos errores léxicos (`Cordo`, `Perso, dos`); no se
  corrigieron mediante aliases ni etiquetas esperadas de esta fixture.
- La pasada final del probe tardó 449,68 s en un host con otras compilaciones y
  carga muy elevada. Ese tiempo no es un benchmark de inferencia aislada.
- Cancelación nativa durante preparación: salida cancelada, cero fragmentos,
  1,51 s. Una entrada sintética estéreo silenciosa a 48 kHz y offset 2,5 s
  devuelve cero fragmentos, duración 4,5 s y ningún anuncio inventado.
- Configuración sin Whisper y compilación del objeto del proveedor portable:
  correctas. No se ejecutó una aplicación Windows/iOS/Android en esta entrega.
- Regresión ejecutada: `section_detection`, `section_detection_service`,
  `section_detection_panel` y `waveform_editing`, las cuatro aprobadas. Cubren
  también la cancelación de un éxito ya encolado a la UI y la invalidación de
  caché por guía, modelo, idioma, offset y versión.
- El panel de producción se ejecutó además con la guía y el modelo reales,
  una raíz temporal nueva y la red bloqueada. El test informó
  `PASS: real Whisper guide reaches transcript UI without Apple or arrangement writes`.
  La captura `whisper-transcript-panel.png` se inspeccionó visualmente: 15
  fragmentos legibles, marcas de voz y acciones de aceptar secciones deshabilitadas.

Resultados privados y logs reproducibles locales:
`output/section-detection-fixtures/whisper-real-guide-v2.json`,
`whisper-cancel.json`, `whisper-silence.json`, `ctest-*.log`;
builds en `/tmp/pw-whisper-*-build.log`. El audio y sus transcripciones no se
incorporan a Git. Las suites con proveedores sintéticos siguen siendo pruebas
de contratos; la inferencia anterior es la evidencia independiente de ASR real.
