---
type: Analysis Contract
title: Assisted Section Detection
description: Propuestas locales revisables sin reemplazar el arreglo existente
resource: src/analysis/SectionDetectionService.h
timestamp: 2026-10-27T00:00:00-03:00
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

El MVP limita el análisis local a 12 horas y 384 kHz; los errores de decodificación,
escritura, datos no finitos y ausencia de música vuelven como error visible.
La mezcla lineal es una herramienta de análisis, no un render para distribuir.

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
Faltan el reconocimiento local con SpeechAnalyzer/SpeechTranscriber, tiempos ASR
finalizados, la comprobación de modelo/idioma español y la evaluación de esa guía
contra stems/click del mismo proyecto. No se declara precisión ni alineación
musical observada antes de esa medición.

La entrega de stems puede añadir evidencia RMS, transitorios, actividad y
similitud mediante un proveedor separado que produzca evidencia en segundos.
El contrato actual separa fuente, estructura, tempo y cues para hacerlo sin
acoplar análisis al motor. No se han añadido detectores por stem ni librerías
de inferencia. Mayor energía no se convierte en Coro ni ausencia de instrumentos
en Verso.
