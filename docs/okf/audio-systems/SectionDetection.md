---
type: Analysis Contract
title: Assisted Section Detection
description: Propuestas locales revisables sin reemplazar el arreglo existente
resource: src/analysis/SectionDetectionService.h
timestamp: 2026-11-06
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
`guideLanguage` (`es-en` inicialmente) exige exactamente un stem confirmado
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
En MSVC Win32, el backend desactiva BMI2 para usar el desempaquetado
alternativo de ggml, dado que el enlace de 32 bits no resuelve `_pdep_u64`.
La transcripción local conserva el mismo modelo y contrato de reconocimiento.
La inferencia CPU se serializa y usa hasta cuatro hilos; prepara audio en ventanas
de 20 s sin retener el PCM completo de una canción. Conserva límites de entrada
de 12 horas y 384 kHz y limita inicialmente el archivo de modelo a 256 MiB.
La lectura, hash, preparación e inferencia están fuera del callback de audio.

### Español e inglés por anuncio

El modo predeterminado **Español e inglés** (`es-en`) compara reconocimiento
en ambos idiomas para cada fragmento de voz de la guía. Usa el mismo modelo
multilingüe local y los mismos tiempos de la fuente; no elige un único idioma
para toda la canción. Las opciones manuales **Español** (`es`) e **Inglés**
(`en`) mantienen una sola pasada en el idioma elegido.

Cada `GuideTranscriptSegment` conserva el texto literal elegido, su idioma
(`language`, `es` o `en`), una confianza opcional y las alternativas evaluadas.
`GuideTranscriptAlternative` contiene texto, idioma, `avgLogProbability` y
`noSpeechProbability`; incluye la hipótesis elegida para poder revisar la
decisión. `GuideTranscript::language` conserva el modo solicitado (`es-en`).
La confianza deriva de probabilidades de tokens y sirve como evidencia
heurística: no representa un porcentaje de exactitud ni demuestra que el
anuncio coincida con un límite musical. El catálogo compartido de nombres e
indicaciones en español e inglés ayuda a interpretar el texto; no sustituye
una transcripción por una secuencia esperada de una canción particular.
Los casos desconocidos continúan revisables.

La versión actual usa `1.9.5:guide-energy-sinc16k-beam5-prompt128-bilingual-13-guide-repetitions`
y añade `:pack-<SHA256>` (o `:pack-unavailable`) a su identidad. La versión
del proveedor distingue este reconocimiento de las inferencias anteriores en
la identidad de caché. Cambiar de modo o de versión no reutiliza un resultado
calculado con otros parámetros. La ampliación del catálogo invalida la pasada
anterior porque el selector bilingüe utiliza ese vocabulario como prior, aunque
el prompt y el modelo sean los mismos. No requiere otro
modelo ni una descarga adicional si el modelo local ya está preparado.
Ambas pasadas se ejecutan en el worker local y respetan cancelación y progreso;
ninguna guía, alternativa o muestra de audio se sube a un servicio.

El gate primario conserva sus 300 ms de silencio para no cortar nombres de
varias palabras. Si el fragmento elegido no es una directiva individual del
catálogo, `GuideSpeechSegmentation.h` puede proponer un reintento sobre pausas
reales de al menos 80 ms, medidas en ventanas de 10 ms del mismo PCM fuente.
Se admiten de dos a ocho hijos, cada uno con al menos 80 ms de actividad, y su
contexto sólo puede incluir silencio del vecino. Cada ventana vuelve a pasar
por Whisper en los idiomas elegidos. Se reemplaza el padre únicamente si
todos los hijos tienen una directiva individual reconocida, confianza al
menos `exp(-1.2)` y probabilidad de no voz de hasta 0,4 en su alternativa
elegida. Un fallo conserva el resultado completo original para revisión.
No se reparten palabras por intervalos iguales ni por BPM. La cancelación,
progreso monótono y límites del proveedor abarcan también este reintento.

Los anuncios explícitos separados por coma o punto y coma también activan
el reintento aunque el parser de etiquetas admita un sufijo de utilidad.
`guideCompoundDirectives` exige de dos a ocho partes exactas del catálogo,
al menos una indicación y como máximo una sección musical. Puede reagrupar
un ordinal, como `Verso, uno`, antes de clasificar las partes. Se conservan
como anuncios únicos las instrucciones completas, por ejemplo `solo, bajo`,
y atributos con conector, como `Coro con toda la banda`.
Para aceptar la separación, las ventanas medidas deben coincidir en número
con las partes y cada hijo debe reconocer la misma familia/ordinal o clave
de instrucción de su parte, permitiendo equivalentes ES/EN. Si falla algo,
el padre se conserva completo como `compound-announcements` para revisión:
no se reduce a la sección y no se pierde silenciosamente su indicación.
El aprendizaje local tampoco puede convertir un compuesto entero en alias
de una sola familia.

La versión 9 añade un segundo reintento acotado sólo para esos compuestos
explícitos. Mide pausas de al menos 60 ms y enumera agrupaciones contiguas de
sus regiones acústicas: así la pausa interna de `All In` no obliga a crear
otra indicación. Cada grupo completo vuelve a reconocerse y debe corresponder
a su parte del padre con los mismos umbrales. Se acepta **una sola partición
válida**, después de comprobar las candidatas; dos particiones válidas son
ambiguas. Ocho regiones permiten como máximo 35 particiones; se reutilizan
decodificaciones del mismo intervalo y se admiten hasta 16 ventanas adicionales.
Agotar ese presupuesto conserva el padre, incluso si ya apareció una candidata
provisional. Sin pausa real no se propone una frontera a partir del número de
palabras, del BPM ni de los tiempos esperados de una prueba.

Si esa separación falla para una cuenta o directiva compuesta reconocida, una última
pasada puede conservar `words`: texto, intervalo y confianza por palabra. Sólo
se adjuntan si el idioma y el texto normalizado coinciden con el padre original;
no se sustituyen su texto, tiempos, alternativas ni confianza. Son intervalos
ASR aproximados, **no ataques de voz exactos**. La caché de esquema 2 admite
esta evidencia opcional (hasta 128 palabras, rangos positivos dentro del padre,
inicios crecientes y confianza válida); entradas anteriores sin `words` siguen
siendo legibles, sin inventar la información ausente.
Esta pasada usa directamente el idioma elegido para el padre. El recolector
agrupa subtokens antes de validar cada palabra: un subtoken de duración cero
puede aportar texto y confianza si la palabra completa tiene algún intervalo
positivo. No extiende ese intervalo; una palabra entera con tiempos colapsados
invalida la evidencia completa y mantiene el anuncio revisable.

### Tiempos independientes con las grabaciones conocidas

`GuideAcousticTemplateMatcher` lee el ZIP de voces distribuido con la app,
sin extraerlo, descargarlo ni modificar los assets de playback. Sólo se activa
en compuestos elegibles de la guía, después de la transcripción inicial. Las
grabaciones se decodifican y convierten al mismo mono de 16 kHz; la búsqueda
FFT considera todos los desplazamientos de muestras y confirma los candidatos
mediante correlación directa. No recibe BPM, compases, guiones de pruebas ni
tiempos esperados. Una voz distinta conserva las rutas de ASR y revisión
anteriores cuando no hay coincidencia física suficiente.

Se valida una reconstrucción conjunta del PCM, con correlación normalizada
condicional de cada componente de al menos 0,94 y proporción de energía
residual de hasta 0,12. Esas medidas no son probabilidades de tokens ni de
exactitud. Los significados observados por ASR deben mantener su orden; el
audio puede aportar una cuenta o indicación omitida únicamente mediante su
grabación física. Se consideran los roles de cuentas/utilidades ya presentes
en los anchors, y no se descubren nuevas principales omitidas. Una directiva
atómica como Verso 2 sigue siendo una sola sección. Intro seguido de un único
Dos sólo cambia a dos marcas si se demuestran ambas grabaciones independientes.
Hay un máximo de ocho anuncios, márgenes entre candidatos competidores y
rechazo de silencio, energía constante, ruido no coincidente, datos no finitos,
ambigüedad o límites agotados.

La recuperación atómica añade la búsqueda `matchKnownGuideAtomic` contra el
pack completo para un anuncio breve aún no resuelto. Requiere un único hijo
físico, correlación de al menos 0,94, residuo de hasta 0,12 y margen de 0,08
frente a otro significado. No anula una alternativa ordinaria ya respaldada
ni toma una palabra de un compuesto como si fuera todo el anuncio. Si no hay
coincidencia, un reintento acotado usa `GuideCatalogueGrammar`, generado desde
el catálogo compartido. Sus hipótesis se guardan en `catalogueAlternatives`,
separadas del ASR literal y con uso exclusivo para revisión. Una probabilidad
condicionada por gramática no se promociona a evidencia suficiente de voz.
El contrato completo está en [CatalogueRecognition.md](CatalogueRecognition.md).

El padre ASR conserva exactamente su texto, rango, idioma, alternativas,
confianza y palabras. La evidencia adicional se guarda en
`acousticAnnouncements` con texto, idioma del asset, identidad de grabación,
inicio/final fuente y correlación; `acousticResidualRatio` conserva el residuo
conjunto. Los finales pueden solaparse realmente aunque los comienzos sean
estrictamente crecientes. Esos hijos no se insertan en `transcript.segments`,
cuyo contrato conserva frases sin solapamiento. La alineación expande una copia
revalidada y propone el compás siguiente sólo para las principales. Las
indicaciones reconocidas y las cuentas mantienen su instante medido exacto.
No presenta correlación como confianza ASR de los hijos. Las sugerencias
musicales añaden la fuente
`known-guide-recording`; el padre permanece disponible para revisión.

La evidencia es opcional en la caché de esquema 2 y está acotada a ocho puntos,
fuente real y contexto del padre de 200 ms previos/300 ms posteriores. Tipos,
rangos, correlación y residuo deben ser finitos y coherentes; un residuo sin
anuncios se rechaza. El SHA del ZIP participa en la versión antes de consultar
la caché. El servicio envía `expectedProviderVersion`, el proveedor compara su
captura y el servicio revalida antes de escribir/publicar, incluidos cache hits.
Un cambio del pack invalida la entrega; si ocurre tras escribir se retira sólo
el JSON recién generado. No se publican cues parciales ni se normaliza bajo una
identidad antigua una inferencia calculada con otras voces.

La caché de esta ruta usa esquema **2** y vive en `guide-transcripts-v2/` dentro
del directorio de análisis. Conserva las entradas v1 existentes sin reutilizarlas
para el reconocimiento nuevo. La clave incluye contenido completo de
audio/modelo, offset, modo de idioma, proveedor y versión. Guarda JSON de
transcripción/metadatos, incluidas alternativas e idioma/confianza por fragmento,
nunca PCM ni audio enviado a una red; no se incorpora a Biblioteca o Play Cloud.
Se comprueban de
nuevo identidad y rangos antes de reutilizarla. Cancelar o cambiar de canción
invalida la entrega al panel; no confirma secciones.

### Preparar el modelo una vez

En el flujo nativo `Detectar secciones`, si falta el modelo local se pide
autorización mediante una tarjeta compacta con `Descargar modelo` y `Ahora no`.
La primera acción prepara el modelo una vez mediante
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
mapa de tempo ni el arreglo confirmado.
El contrato activo de interpretación/alineación se identifica como
`sections-mvp-11-count-word-boundaries`: principales al compás siguiente,
indicaciones en su inicio hablado y cuentas sin desplazamiento. Los anuncios
sin resolución suficiente quedan como puntos pendientes para elegir una
marca del catálogo, sin crear un bloque principal desconocido.
`makeInitialGuideAlignmentOptions(transcript, musicalContext)` toma el comienzo
del primer fragmento hablado como candidato a `originSeconds`, incluso si ese
fragmento es un conteo, y puede refinar su fase mediante las cuentas realmente
observadas. Sólo usa cuentas atómicas o evidencia acústica validada; excluye
límites de palabras que ya dependan de la cuadrícula propuesta. Si el borrador
inicial ya puede expandir un conteo compuesto mediante sus intervalos de
palabra y la cuadrícula, conserva el origen completo, incluso si hay cuatro
cuentas atómicas concordantes: refinarlo también movería aquellas cuentas
cuya posición ya depende de la cuadrícula.

Se requieren al menos cuatro cuentas medidas en pulsos distintos y un rango
de al menos tres pulsos. El ajuste es la mediana de sus residuos respecto del
pulso más próximo, con magnitud máxima de `min(40 ms, duración del pulso / 4)`.
Deben concordar al menos cuatro cuentas y un 75 % de las observaciones dentro
de 12 ms de esa mediana; la desviación absoluta mediana no puede superar 8 ms.
Los márgenes se acotan también para pulsos muy cortos. El primer conteo puede
ser un valor atípico: no obliga por sí solo a conservar una fase incorrecta.
Un cambio de tempo o métrica durante el conteo, tiempos irregulares o consenso
insuficiente conservan el candidato inicial. No se impone un origen cero a
todas las canciones ni se deducen números ausentes.

Este ajuste afecta sólo a la cuadrícula, nunca a los tiempos medidos de las
cuentas. El origen es el pulso 1 del compás 1 y sigue siendo revisable. Moverlo
manualmente reconstruye el borrador a partir de la voz original, sin volver a
refinar esa elección.

La cuadrícula usa BPM original y `SongTempoMapSnapshot` en segundos fuente.
Cada pulso dura `(60 / BPM) * 4 / denominador` y cada compás contiene el
numerador de pulsos. Cambiar BPM conserva la fase fraccionaria del pulso y del
compás; cambiar métrica inicia un compás nuevo, como `PracticeTimelineRuler`.
Los huecos del mapa usan el ritmo base. La enumeración no depende del zoom ni
de una cantidad de píxeles: incluye todos los pulsos y compases desde el origen
hasta la duración. Admite hasta 250000 marcas; exceder el límite devuelve un
error y elimina la cuadrícula parcial. Un mapa inválido, desordenado o
superpuesto se rechaza. No se sustituyen metadatos inválidos por tempo supuesto.

La propuesta automática de una sección principal se ubica en el compás
siguiente al que contiene el anuncio. No equivale a sumar una duración
completa de compás a una voz que empieza a mitad del compás.
No hay selector de cero/dos compases; los ajustes explícitos de
origen y arrastre de una marca siguen disponibles en la revisión visual.
Antes de clasificarlo se ajusta únicamente el ancla de cálculo a la frontera
de compás más cercana si está a un máximo de 20 ms: el detector de voz trabaja
con ventanas de 20 ms y Whisper entrega ticks de 10 ms. El tiempo hablado
original no cambia. Así, un anuncio situado 10 ms antes de una línea se
considera sobre esa línea y la anticipación de uno propone la siguiente. Esto
es una elección revisable, no una medición de la anticipación real de la voz.
Una sección fuera del rango o que no deje al menos 0,25 s produce un error de
revisión. Una indicación válida requiere su inicio hablado dentro de la
duración fuente, sin buscar un compás posterior. No se retrocede a un compás
anterior ni se recorta el destino de una principal para hacerlo caber. El
error impide confirmar la revisión completa. El resto del
borrador permanece visible para ajustar el origen. El inicio hablado original,
desplazamiento y anticipación resultante permanecen en la propuesta.

Los destinos musicales ausentes de las principales se identifican por anuncio en
`GuideAlignmentDraft::timingIssues`, un vector de `GuideEntryTimingIssue`
con `id`, `rawText`, `rawTimeSeconds` y `secondary`. `canReviewGuideAlignment`
distingue esos problemas corregibles de los errores estructurales del
borrador. Una indicación cuyo inicio hablado es válido no genera una incidencia
por falta de un compás posterior, aunque esté cerca del final. Los helpers
`moveGuideSecondaryCue` y `discardGuideSecondaryCue` conservan los gestos por ID
y cualquier incidencia restante de otros anuncios. La UI reaplica movimientos y
descartes por ID al reconstruir, antes de validar el error final. La aceptación
atómica rechaza cualquier `timingIssues` pendiente aunque se haya vaciado
manualmente el texto de `error`; borrar el aviso no valida un destino ausente.

`data/GuideVocabulary.h` reúne las 17 familias de sección, aliases ES/EN y
utilidades usadas por la detección, el editor y la clasificación de Guía Dyn.
Incluye Turnaround, Exhortation y Rap, además de Repetir→Tag y Pre→Pre-Chorus.
Los valores serializados de las 14 familias previas permanecen estables;
los tres tipos nuevos se añaden después. Tener una entrada de vocabulario no
garantiza una grabación en todos los idiomas del pack instalado.

Los nombres reconocidos usan `normalizeGuideCue`, incluidos sus ordinales,
y la capa `GuideSectionInterpretation` resuelve correcciones de reconocimiento.
Una secuencia de cuentas después de una etiqueta no se interpreta como ordinal
y sufijo: `Intro, dos, tres, cuatro` no es `Intro 2`. Cuando se vuelve a
reconocer desde sus pausas, la etiqueta y las cuentas reciben tiempos propios.
La repetición de Intro, Interludio u otras familias sin variantes numeradas
no añade un número a la etiqueta; los ordinales realmente hablados siguen
siendo válidos. Se conserva la convención de numeración de los Refrain.
La equivalencia confirmada «For a frame»→Refrain se aplica a anuncios completos
normalizados, conservando el texto ASR y su identidad. Renombrar una propuesta
actualiza también su tipo y ordinal cuando el nombre pertenece al catálogo.
Las correcciones aceptadas se guardan localmente en
`MTControls/analysis/guide-section-corrections-v1.json`, fuera de la caché ASR
y de Play Cloud. El archivo guarda la familia, no el ordinal de una aparición:
los siguientes Refrain se numeran al reconstruir la canción. Sólo se aprende
después de aplicar una confirmación válida; cancelar no escribe correcciones.
El store usa formato versionado y reemplazo atómico; un archivo inválido o de
versión desconocida no se sobrescribe. Las anotaciones fusionadas ambiguas
no enseñan equivalencias a partir de una única corrección de nombre.
Las variantes próximas a una familia pueden ordenar opciones de revisión,
pero un candidato tentativo no crea una sección. Queda en
`pendingAnnouncements` y requiere elección explícita o descarte. Las
alternativas ASR se conservan y las reparaciones de interpretación no
reescriben el texto elegido por el decoder. Los alias ordinarios del catálogo
sí participan en su prior léxico acotado y requieren una nueva versión del
proveedor al cambiar.

`interpretGuideAnnouncement` clasifica primero conteos e indicaciones y luego
secciones, con la misma ruta en la alineación nativa y la evaluación del corpus.
Las correcciones confirmadas de anuncios completos son `Base`→Bass,
`Thor`→4, `Swin`→Swell, `Course 2`→Chorus 2, `Exotación`→Exhortation,
`Enterlude`→Interlude y `Wrap`/`Prab`→Rap. No sustituyen palabras dentro de
frases largas ni cambian el texto, tiempos, confianza o identidad ASR.
La revisión de MI TESTIMONIO añade `Balsa`→Break (Pausa) y
`Entra a batería`→Drums In como anuncios completos; conservan su instante
hablado como evidencia y no crean un límite musical. Su destino propuesto
coincide con ese inicio hablado, salvo una corrección visual explícita.
Las equivalencias específicas de intensidad ensayadas durante la revisión de
ALABA en el contrato 9 se retiraron. La resolución usa alternativas tipadas,
evidencia acústica del catálogo o una elección explícita de revisión, sin una
tabla de errores añadida para esa canción.
`Adlib`, `Subetono`, `Teclados`, `Pulsos` y `Coros` se resuelven como variantes
ordinarias del catálogo; las vocales acentuadas mayúsculas y descompuestas se
normalizan también, incluido `Última vez`. La frase errónea que enumeraba
varios coros no tiene una equivalencia codificada: se recupera Chorus 2 cuando
su alternativa real `Coros 2` satisface los umbrales acústicos existentes.
Sin esa evidencia, queda pendiente de revisión. Estas adaptaciones proceden
del informe humano; la aplicación no importa el HTML ni sus respuestas al
almacén de aprendizaje local.

`guideCatalogEntries()` genera el conjunto cerrado desde `GuideVocabulary.h`:
17 familias y sus ordinales permitidos, 26 utilidades, utilidades literales
declaradas y cuentas 1–7. `guideCatalogChoices` ordena todas las opciones por
evidencia. `interpretGuideAnnouncement` puede usar alternativas ordinarias
ES/EN para resolver cualquiera de los tres roles, con las mismas puertas de
voz: puntuación finita entre −1,2 y 0, no-voz entre 0 y 0,4 y diferencia máxima
de 0,2 respecto del texto seleccionado. Alternativas débiles, similitud de
texto y alternativas condicionadas por gramática sólo ordenan opciones con
`supported=false`; su ranking no es confianza de reconocimiento.

Un desconocido, compuesto sin resolver, candidato tentativo o principal con
tipo/ordinal fuera del catálogo se conserva en `pendingAnnouncements`, con
ID, texto crudo, rango fuente y candidatos. La onda lo muestra como un punto
**Elegir marca**, sin dividir la canción. `chooseGuideCatalogMark` exige una
entrada canónica y aplica el tiempo de su rol: próxima entrada de compás sólo
para principales; inicio fuente para indicaciones o cuentas. Las elecciones
se guardan por ID en `GuideAlignmentOptions::catalogueSelections` y se
reaplican al mover el origen, junto a ediciones manuales y descartes.
`discardGuidePendingAnnouncement` retira únicamente ese anuncio. Confirmar
queda bloqueado mientras haya pendientes, aun si el texto de error está
vacío. No se deducen Intro o Final de silencios o de la posición. Véase
[CatalogueRecognition.md](CatalogueRecognition.md).

Las indicaciones reconocidas se conservan además como `secondaryCues` en el
borrador. `normalizeGuideUtilityCue` devuelve su nombre canónico de guía
(`Drums In`, `All In`, `Build`, `Breakdown`, etc.), el texto original, su
tiempo hablado y la confianza ASR opcional. `rawTimeSeconds` retiene ese
tiempo; `timeSeconds` empieza exactamente en ese mismo instante, o en su
corrección visual explícita. No se cuantiza ni retrasa al compás siguiente.
No equivale a convertirlas en
secciones musicales: cada una es un punto independiente y no cambia los
límites ni la numeración de las propuestas principales. Los conteos se
distinguen con `isGuideCountCue` y también se conservan en `secondaryCues`.
`normalizeGuideCountCue` convierte un número individual reconocido en español,
inglés o dígitos del 1 al 7 en su clave numérica nativa. Conserva exactamente
el texto, tiempo y confianza originales. Cada anuncio es un punto individual:
dos «dos» en tiempos distintos son dos marcas, sin añadir un «uno» ausente.
Una frase de conteo compuesta o genérica sin resolución temporal suficiente,
o fuera del rango de marcas, permanece como un punto pendiente con su texto
literal para elegir o descartar. Sin tiempos por palabra o evidencia acústica
no se inventan subdivisiones ni se guarda un cue ajeno al catálogo.

Una cuenta compuesta con intervalos de palabras admite propuestas individuales
sólo para los números explícitos del 1 al 7, mínimo dos y máximo ocho. Puede
precederla una etiqueta exacta sin variantes numeradas, como Intro. El texto
por palabra debe reproducir todo el padre y cada cuenta necesita suficiente
confianza. La primera opción es el único pulso dentro de cada intervalo, en
orden creciente y antes del final de la fuente. Si falla esa asignación,
los números consecutivos pueden recuperarse como grupo. Se requiere al menos
una palabra con un único pulso exacto y una sola secuencia contigua compatible
con todas las palabras. Para un intervalo sin pulso se admite una distancia de
hasta 5 ms (medio paso del reloj de Whisper), dentro del rango del anuncio. Un
intervalo con dos pulsos exige que al menos uno quede a 5 ms o menos de su
borde. Una ambigüedad mayor o la falta de intervalos deja el grupo en revisión.
El padre permanece en la transcripción; su etiqueta usa el instante y la
identidad originales. `rawTimeSeconds` de estas cuentas es el límite inferior
del intervalo ASR y `timeSeconds` es el pulso propuesto, sin presentarlo como
inicio vocal exacto. Cambiar el origen puede recalcular esta propuesta dentro
de la evidencia; los cambios manuales y descartes se reaplican por ID estable.
Renombrar la etiqueta recuperada nunca enseña el compuesto entero como alias.

Los conteos individuales conservan el comienzo hablado en segundos fuente.
Las instrucciones reconocidas también conservan exactamente ese comienzo:
cambiar el origen no mueve ninguno de esos puntos medidos. Las correcciones
manuales de posición se reaplican por ID estable.
`moveGuideSecondaryCue` permite ajustar una sola indicación al pulso más cercano
de la cuadrícula o libremente con Alt. Conserva el tiempo ASR original y rechaza
tiempos no finitos o fuera de la duración fuente. Los nombres y posiciones
corregidos y los descartes se reaplican por ID al recalcular el borrador.

`appendConfirmedGuideSuggestions` prepara una única aceptación aditiva de
secciones y puntos. Las secundarias se guardan como
`SongArrangementSnapshot::cues`, con nombre canónico cuando existe una
instrucción equivalente para resolver la grabación de Guía Dyn. Los conteos
individuales se guardan con `name` numérico y `countEnd=0`; no se infiere una
secuencia de pulsos ni se rellenan huecos de la transcripción. Las
instrucciones conocidas sin equivalente exacto y los nombres manuales libres
conservan su texto para revisión; no se garantiza una grabación para esos
nombres. El texto ASR continúa en la transcripción. Se anclan por
`partId+offsetSec` a la sección final que contiene el punto en `[startSec,endSec)`;
fuera de una sección conservan una posición absoluta. Se preservan todas las
secciones y cues previos. Se permiten propuestas sólo secundarias y se respeta
el límite nativo de 512 eventos de cue, incluidos conteos expandidos existentes.
Un conflicto invalida toda la aceptación. El editor sigue usando una sola
transacción, un Deshacer y la persistencia de arrange existente; analizar,
arrastrar, renombrar o cancelar el borrador no publica instrucciones al motor.

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
  La versión evaluada entonces `guide-energy-sinc16k-beam5-prompt128-2` habilita contexto de
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

## Evidencia del incremento bilingüe — 4 de noviembre de 2026

- Build nativo macOS arm64 Release de app, proveedor, probe y pruebas completo:
  `/tmp/pw-bilingual-build.log`. La app actualizada se reinició y se observó su
  revisión de propuestas sobre la onda de **IMPRESIONANTE**, conservando el tema.
- CTest: **5/5** suites (`section_detection`, `section_detection_service`,
  `section_detection_panel`, `guide_recognition_selection`, `waveform_editing`)
  en 5,24 s; dominio 285 comprobaciones y selector 22. Evidencia:
  `/tmp/pw-bilingual-ctest.log`. Los proveedores falsos validan contratos,
  no certifican exactitud de reconocimiento.
- Se ejecutó de nuevo la guía local de IMPRESIONANTE con el modelo multilingüe
  instalado y `es-en`, versión `bilingual-3`, mediante el mismo servicio que la
  app. El proceso se ejecutó bajo `sandbox-exec` con `(deny network*)`:
  **47,97 s**, `cacheHit=false`, **27 anuncios**, cada uno con las dos
  hipótesis originales ES/EN y sus valores del decoder. Estos idiomas identifican
  la pasada de reconocimiento elegida, no una detección lingüística certificada.
- A 69 BPM, 4/4, origen en el primer anuncio (0 s) y anticipación de un compás,
  el borrador tiene **14 propuestas: 11 reconocidas y 3 para revisar**. Cinco
  conteos y ocho indicaciones conocidas permanecen en la transcripción, sin
  introducir límites musicales. Las tres frases de 219,12; 233,04 y 246,96 s
  siguen como `Revisar: For a frame.`; no se convirtió ese error en un alias
  de Refrain ni se atribuyó una exactitud musical a las probabilidades ASR.
- Una segunda petición idéntica devolvió `cacheHit=true` en 0,42 s, con los
  mismos fragmentos y alternativas. La app mostró esa revisión con
  Confirmar/Cancelar; las secciones no se confirmaron. Las huellas de los bytes
  originales de guía/modelo, el arreglo guardado, BPM y métrica permanecieron
  iguales. La caché anterior v1 se conservó.
- Cancelación nativa del reconocimiento bilingüe: salida 2, `cancelled=true`,
  1,82 s y ningún fragmento parcial publicado. Este chequeo no escribió caché.

Los artefactos privados de esta ejecución viven en
`output/section-detection-fixtures/impresionante-bilingual-20261104.json`,
`impresionante-bilingual-cache-20261104.json` y
`impresionante-bilingual-cancel-20261104.json` (excluidos de Git).
Los logs están en `/tmp/pw-impresionante-bilingual*.log`.
El probe acepta `--cache-directory` para ejecutar el servicio real y
`--bpm`/`--meter` para exportar su propuesta. Su origen coincide con la primera
marca hablada; su alcance es la duración de la guía y tempo constante, sin el
mapa de tempo ni la posible cola musical del proyecto. La UI sí usa esos datos
del proyecto. La cuantización y la anticipación requieren revisión visual;
este incremento de idiomas y vocabulario no valida sus límites contra la música.

## Evidencia de indicaciones secundarias — 4 de noviembre de 2026

- Build nativo macOS arm64 Release de app/probe y cinco suites completo;
  cierre con el ajuste de filas en `/tmp/pw-secondary-layout-build.log`.
  CTest final **5/5** en 5,19 s:
  `/tmp/pw-secondary-layout-ctest.log`. Dominio: **363 comprobaciones**.
- El probe ejecutó el servicio real con la red bloqueada y reutilizó la
  transcripción bilingüe validada de IMPRESIONANTE (`cacheHit=true`, 0,38 s).
  Sus 27 segmentos y las 14 propuestas principales son idénticos a la evidencia
  anterior. Ahora añade **8 secundarias**: Drums In ×1 (74,78 s), Breakdown ×2
  (100,86 y 293,90 s), All In ×3 (109,56; 165,22 y 266,08 s) y Build ×2
  (151,30 y 262,60 s). No se ejecutó una nueva inferencia de voz para este chequeo.
  Resultado privado: `output/section-detection-fixtures/impresionante-secondary-cues-20261104.json`.
- Se reinició la app compilada y se observó la revisión de IMPRESIONANTE con
  14 marcas principales y 8 secundarias ámbar sobre la misma onda. Se restauraron
  mediante el menú de renombrar las tres correcciones manuales Refrain 1/2/3,
  tras confirmar el usuario que eran los únicos cambios del borrador. Quedó
  abierta sin confirmar; el arreglo guardado, audio y modelo permanecieron intactos.
- La regresión de filas comprueba tres etiquetas largas y ocho indicaciones
  en un cuerpo de onda de 130 puntos, sin solapamientos entre ambos grupos.
  La revisión nativa confirmó la separación. Las pruebas adicionales cubren
  arrastre con pulso/Alt sin seek ni publicación, nombres y descarte, Cancelar,
  aceptación conjunta y sólo cues, Undo/Redo, anclas, límites, IDs y preservación
  de conteos existentes dentro del máximo de 512 eventos expandidos.
- La captura `output/section-detection-fixtures/secondary-cues-preview.png`
  pertenece a un fixture sintético del componente de producción (tres Refrain
  y ocho indicaciones), no al audio real de IMPRESIONANTE. Se genera con
  `PW_GUIDE_SECONDARY_CAPTURE_PATH` en la suite waveform.

## Evidencia de conteos visibles — 4 de noviembre de 2026

- Build final macOS arm64 Release de app, probe y cinco suites correcto:
  `/tmp/pw-count-final-build.log`. CTest final **5/5** en 7,92 s:
  `/tmp/pw-count-final-ctest.log`. Incluye el color azul de conteos literales
  al confirmar, sin dividirlos ni generar tiempos para sus palabras.
- El servicio real, ejecutado con la red bloqueada, reutilizó la transcripción
  bilingüe de IMPRESIONANTE (`cacheHit=true`, 2,32 s). Sus 27 segmentos y las
  14 propuestas principales son idénticos al resultado de secundarias anterior;
  no se repitió la inferencia ASR. El borrador incorpora ahora **13 puntos**:
  las ocho indicaciones y cinco conteos individuales: `1` a 0,00 s, `2` a
  1,74 s, otro `2` a 4,34 s, `3` a 5,20 s y `4` a 6,08 s. Se conserva el
  segundo `DOS` reconocido; no se completó una secuencia esperada.
- Resultado privado:
  `output/section-detection-fixtures/impresionante-count-suggestions-20261104.json`;
  log `/tmp/pw-impresionante-count-suggestions.log`. El probe distingue los
  puntos con `kind=count` o `kind=instruction` sin modificar el texto crudo.
- La app nativa actualizada mostró los cinco conteos azules y las ocho
  indicaciones ámbar sobre la misma onda. A petición del usuario se deshizo
  la confirmación mediante el historial nativo y se volvió a la revisión.
  Se restauraron Refrain 1/2/3 en el borrador; la biblioteca quedó con cero
  secciones y cero cues guardadas, pendiente de confirmar.
- La suite `waveform_editing` aprobó la regresión de cuerpo de 130 puntos:
  cinco conteos y ocho instrucciones seleccionables en revisión y después
  de confirmar/reabrir, etiquetas accesibles completas, tiempos y campos
  originales intactos, arrastre de una cuenta y Deshacer/Rehacer. Esa
  confirmación/reapertura se ejecutó en el fixture del componente nativo;
  la revisión real de IMPRESIONANTE permanece sin confirmar.

## Catálogo, correcciones y prueba de voces — 4 de noviembre de 2026

- Build nativo macOS arm64 Release de app, probes y siete suites correcto:
  `/tmp/pw-learned-catalog-build-final.log`. CTest final **7/7** en 7,54 s:
  `/tmp/pw-learned-catalog-ctest-final.log`. Incluye catálogo compartido,
  aprendizaje local tras aceptar, no escritura al cancelar, archivos inválidos,
  tipos/ordinales, tolerancia de compás, candidatos tentativos y nombres nativos
  al confirmar. Los fixtures no certifican alineación musical por escucha.
- Se reinició la app compilada y se ejecutó de nuevo Sugerir secciones para
  IMPRESIONANTE: se observó el modal de progreso y después el borrador con
  **14 secciones reconocidas**, Refrain 1/2/3 automáticos y **13 puntos**.
  La biblioteca permanece con cero partes/cues guardadas y 69 BPM. El probe
  posterior reutilizó la nueva caché en 0,40 s con la red bloqueada:
  `impresionante-learned-catalog-20261104.json`. Sus 27 segmentos crudos son
  idénticos a la evidencia anterior; cambian nueve entradas por la tolerancia.
  Verso 1 queda en compás 7 (20,869565 s), Coro 1 en 23 (76,521739 s) y los
  tres Refrain en 65/69/73 (222,608696; 236,521739; 250,434783 s).
  La revisión queda abierta sin confirmar; estos límites requieren escucha.
- `scripts/build_guide_recognition_corpus.py` prepara primero un plan con
  `--dry-run --output-dir <directorio>` y luego lo ejecuta con `--execute`.
  Usa el ZIP distribuido y el catálogo compartido, FFprobe para cada entrada y
  FFmpeg para decodificar a mono PCM16/16 kHz. Deduplica únicamente bytes MP3
  idénticos; conserva todos sus aliases en el manifiesto. No genera voces.
- El ZIP contiene **230 MP3 y 121 fuentes únicas**: EN 62 y ES 59, con 69
  anuncios principales (17 familias y variantes), 38 utilidades y 14 conteos.
  El WAV único dura **396,6875 s** (6.347.000 frames), con dos segundos exactos
  entre clips y un segundo inicial/final. Las 121 regiones PCM y los silencios
  se verificaron por bytes; el ZIP original quedó intacto. Las grabaciones
  ausentes por idioma, incluido Turnaround ES, constan en `missingLanguageClips`.
- Se ejecutó `MTControlsGuideTranscriptionProbe` con el modelo instalado,
  `--language es-en`, una caché nueva y `(deny network*)`. Proveedor
  `1.9.5:guide-energy-sinc16k-beam5-prompt128-bilingual-4-catalog17`:
  **202,60 s**, `cacheHit=false`, **121 segmentos**, sin cancelación. Los nombres
  esperados y el orden de referencias no se suministraron al transcriptor.
- `MTControlsGuideCorpusEvaluationProbe --manifest <manifest.json>
  --transcript <whisper.json> --output <evaluation.json>` compara después por
  solapamiento temporal, familia y ordinal. Exige al menos 30 ms y el 50 % del
  segmento dentro de un solo clip. Candidatos tentativos, omisiones, divisiones,
  referencias desconocidas y segmentos ambiguos no se acreditan como aciertos.
  El resultado fue **106/121 coincidencias directas con el catálogo** y
  **107/121 con interpretación**, incluyendo «For a frame»→Refrain.
  EN: 56/62 en ambas; ES: 50/59 directas y 51/59 interpretadas. Quedan **14**
  casos pendientes, uno tentativo; no hubo omisiones, divisiones ni segmentos
  fuera de las referencias. Esto mide esas voces aisladas, no exactitud sobre
  canciones con música o voces distintas.
- Entre los pendientes hay errores ASR como `Course 2`, `Thor` y `Wrap`, y
  lagunas de interpretación como el texto correcto `Última vez`. No se
  añadieron correcciones derivadas del corpus para inflar esta medición.
- Artefactos locales excluidos de Git en
  `output/guide-recognition-corpus-20261104/`: WAV, plan/manifiesto, hashes,
  índice CSV/SRT, transcripción cruda, evaluación, verificaciones y reporte HTML.
  `scripts/report_guide_recognition.py --evaluation <evaluation.json>
  --audio <all-guide-clips.wav> --output-dir <directorio>` genera HTML y CSV
  completos, con escucha de cada región y filtros. Log ASR:
  `/tmp/pw-guide-corpus-whisper.log`. El check genérico de medios tuvo cero
  fallos y avisos esperados por 16 kHz y subtítulos externos; la verificación
  específica del corpus comprueba el formato requerido y cada frame.

### Revisión humana como insumo de código

`scripts/review_guide_recognition.py --evaluation <evaluation.json> --manifest
<manifest.json> --catalogue <catalogue.json>` genera
`guide-recognition-review.html`. Permite revisar los 121 clips, filtrar los 14
pendientes, escuchar cada región, elegir entre las 17 familias, 26 indicaciones
y 7 conteos, separar un ordinal, añadir notas y solicitar una entrada ausente.
Cada decisión requiere selección y validación explícitas; ninguna comienza
validada. La referencia del archivo y la transcripción ASR siguen separadas.

Este formulario recopila respuestas para adaptar posteriormente el catálogo,
los aliases y las reglas **en el código**. No importa reglas al vocabulario
runtime ni cambia el arreglo o el modelo de voz. «Falta en catálogo» registra
una solicitud de desarrollo, no una definición nueva aplicada automáticamente.

`scripts/serve_guide_review.py --directory <carpeta-del-html-y-wav> --manifest
<manifest.json> --evaluation <evaluation.json> --catalogue <catalogue.json>
--port 0` sirve la revisión sólo en loopback. `POST /api/review` guarda un
snapshot atómico en `guide-review-decisions.json`, ligado al SHA del corpus y
a sus IDs. Incluye decisiones, validación humana, ordinales y notas; no acepta
rutas de almacenamiento desde el cliente. El servidor valida origen/Host,
tipos, tamaños e IDs y permite Range/HEAD para el audio. No hay endpoint de
aplicación de reglas. En modo archivo, la página conserva el borrador en el
navegador y permite exportar/importar JSON para continuar la revisión.

Verificación: 36 comprobaciones HTTP aisladas aprobadas con Python 3.12
(`/tmp/pw-guide-feedback-server-test.log`). En Chrome se eligió una referencia,
se validó y guardó una respuesta temporal; al recargar se recuperó exactamente
esa decisión. La carpeta de QA está separada de las respuestas del usuario.
Las suites de dominio e interpretación previas volvieron a pasar (2/2 en
0,84 s; `/tmp/pw-guide-feedback-regression-ctest.log`).

### Aplicación de las 14 respuestas y nueva inferencia — 4 de noviembre de 2026

Se validaron los 14 IDs confirmados de `guide-feedback-8e0e3fe399be.json`
contra el manifiesto y el SHA-256 del WAV. Las decisiones se convirtieron en
alias ordinarios, reparaciones completas de ASR y pruebas de regresión del
código; el JSON del informe no se carga en ejecución ni escribe el store local.
La interpretación usa `sections-mvp-4-reviewed-cues-next-bar`. El proveedor
Whisper pasa a `1.9.5:guide-energy-sinc16k-beam5-prompt128-bilingual-5-catalog17`
porque los alias ordinarios afectan el prior léxico del selector bilingüe.

- Sobre el JSON anterior sin modificar, el nuevo intérprete resuelve **121/121**
  casos (111 con el catálogo directo). Las 14 respuestas se verificaron por ID,
  clase, clave y ordinal. La lista alucinada se recupera desde la alternativa
  real `Coros 2`; sin una alternativa válida se mantiene para revisión.
- Se ejecutó nuevamente el WAV original con el servicio de producción, mismo
  modelo, caché nueva y red bloqueada: **201,40 s**, `cacheHit=false`, 121
  segmentos. Resultado: **112/121** con el catálogo directo y **121/121** con
  interpretación; nueve emplean equivalencias confirmadas. EN: 62/62; ES: 59/59.
  No hay omisiones, segmentos extra, divisiones ni tentativas pendientes.
  La única diferencia de texto frente a la ejecución anterior es la selección
  de `Coros 2.` en lugar de la lista alucinada, conservando ambas alternativas.
- Es una prueba sobre el corpus que se revisó para adaptar el código, no una
  estimación independiente de precisión sobre canciones o voces nuevas.
  `output/guide-recognition-reviewed-20261104/` conserva transcripción nueva,
  evaluación, comparación por ID, informe HTML/CSV y logs de compilación/pruebas.
- La alineación automática fija el compás siguiente; desaparece el selector
  0/1/2, manteniendo origen y arrastre manual. La tolerancia máxima de 20 ms
  sigue acotada por la separación local de compases. Se comprobaron las 17
  familias en varias métricas y BPM, fronteras tempranas/tardías, mapas variables,
  finales sin espacio y la permanencia de indicaciones/cuentas en su instante.
- App macOS y probes compilados; **7/7 suites aprobadas en 11,27 s**.
  `next-bar-preview.png` muestra el componente nativo de prueba con su regla,
  origen, marcas y Confirmar/Cancelar. Esta comprobación no reinicia ni confirma
  el borrador de la app que el usuario tenga abierto.
- También se retranscribió **IMPRESIONANTE** sin caché en **47,51 s**:
  27 anuncios, 14 principales y 13 puntos secundarios. Con 69 BPM, 4/4 y
  origen cero, se verificaron las 14 entradas contra la fórmula de compás
  siguiente y la tolerancia; las 13 secundarias conservan el tiempo hablado.
  Coro 1: anuncio 73,04 s → compás 23 a 76,521739 s. Refrain 1/2/3:
  compases 65/69/73. `impresionante-verification.json` registra la comprobación.
  Es análisis local de la guía: no confirma secciones ni sustituye la revisión
  auditiva de la entrada musical en la app.

### MI TESTIMONIO: cuentas y anuncios agrupados — 4 de noviembre de 2026

Se inspeccionó la caché real de la guía a 97 BPM y 4/4. La pasada anterior
tenía 26 fragmentos: `intro, dos, tres, cuatro` se interpretaba erróneamente
como Intro 2, al aceptar las cuentas restantes como sufijo de utilidad.
También agrupaba `Entra batería, puente` y `Subir intensidad, coro`.
La medición local del archivo original encontró pausas de 123/162/164 ms
entre Intro y las cuentas, 210 ms entre batería y puente y 92 ms entre
intensidad y coro: todas quedaban dentro del gate primario de 300 ms.

El nuevo proveedor ejecutado sin caché y con red bloqueada produjo **31
fragmentos en 59,48 s**, **18 secciones y 13 puntos secundarios**, sin etiquetas
principales pendientes de reconocimiento. Las cuentas reales son:
**1 a 0 s, 2 a 1,22 s, 2 a 3,09 s, 3 a 3,71 s y 4 a 4,33 s**. No se añaden
otras cuentas a partir del tempo. Intro se reconoce por separado a 2,47 s;
ambos Interludios conservan su nombre sin sufijo numérico.

`Balsa.` permanece como salida literal de Whisper y se interpreta como Break
(Pausa), punto a 75,46 s. Entra batería a 144,75 s y Puente a 145,98 s son
anuncios independientes; el Puente se propone a 148,453608 s. Subir intensidad
a 209,08 s y Coro a 210,31 s son independientes; el Coro se propone a
212,783505 s. La variante `Entra a batería` a 201,64 s también es Drums In.
Las 18 entradas cumplen el compás siguiente con tolerancia; los 13 puntos
conservan su tiempo hablado. La separación usa PCM y nueva inferencia, no
un reparto artificial de palabras del JSON anterior.

`output/mi-testimonio-20261104/` conserva la caché anterior, ambas salidas,
mediciones de silencio, `verification.json` y un informe HTML con la guía
original. La inferencia y los probes no cambian ni confirman el arreglo
de la app abierta.

La app y los probes macOS compilaron y las siete suites quedaron aprobadas.
Se corrigió una cadena UTF-8 de la nueva prueba de segmentación y se repitió
esa suite; las otras seis ya estaban aprobadas. La nueva inferencia completa
del corpus anterior tardó **201,34 s**, sin caché: conserva **121/121**
interpretados (112 directos), sin omisiones, divisiones, extras o tentativas.
La verificación independiente encontró que los 23 anuncios ajenos a las tres
separaciones conservan exactamente texto, tiempos, idioma y confianza, y que
los 31 anuncios del resultado nuevo aparecen una sola vez entre principales
y secundarios.

### VI LA LUZ: puntos temporales y cuentas sin silencio — 4 de noviembre de 2026

La captura mostró dos problemas diferentes. El primer punto azul se desplazaba
horizontalmente al distribuir su etiqueta, pese a conservar 0,02 s en el
borrador. La distribución compartida de revisión y edición guardada ahora
fija siempre el punto en `sourceX`; sólo desplaza la etiqueta y dibuja su
conector. La selección, el arrastre, la confirmación y Deshacer se comprobaron
en el componente nativo, incluido el borde izquierdo de la ventana.

La guía original, 116 BPM y 4/4, conserva `intro, dos, tres, cuatro.` como un
fragmento de 2,16–4,12 s: Intro y Dos se solapan, y las otras pausas son de
60/63 ms. Los timestamps simples de Whisper adelantan algunas palabras unos
200 ms; DTW y `split_on_word` no dieron ataques válidos en el diagnóstico y
no se usan como posiciones exactas. La pasada adicional conserva intervalos
ES de Dos 2,53–2,78, Tres 2,93–3,27 y Cuatro 3,43–3,96 s. Cada uno contiene
un solo pulso de la cuadrícula con origen 0,02 s.

La nueva inferencia local sin caché tardó **48,37 s**: conserva los 25
fragmentos, textos y tiempos anteriores y añade evidencia al compuesto.
Las propuestas son **1 a 0,020 s, 2 a 1,060 s, 2 a 2,606207 s,
3 a 3,123448 s y 4 a 3,640690 s**. Las tres recuperadas quedan a 4,79/7,55/7,31
ms de los ataques del click medidos por umbral. La primera marca conserva el
inicio vocal detectado, 20 ms después del comienzo del archivo de click.
Intro se propone independientemente a 4,157931 s, compás 3. No se modifica
el mapa ni se inventa un número ausente.

App/probes macOS compilados; **7/7 suites en 10,48 s**. La recuperación real
de caché conserva exactamente `words` y las propuestas. MI TESTIMONIO volvió
a pasar por el modelo sin caché y conservó sus 31 fragmentos, 18 secciones,
13 puntos y todos sus nombres/posiciones. La interpretación del corpus previo
permanece en 121/121; para esa comprobación se reutilizó su ASR real anterior,
sin afirmar una inferencia nueva del corpus. Diagnósticos, caché aislada, JSON,
comparación con click, captura del componente e informe HTML están en
`output/vi-la-luz-20261104/`. La app abierta no se reinició ni se confirmó su
borrador durante estas pruebas.

### VI LA LUZ: Toda la banda independiente — 4 de noviembre de 2026

La guía contenía tres `Interludio, toda la banda.`. El parser permitía el
sufijo de utilidad, por lo que el proveedor consideraba cada frase un anuncio
musical único y no intentaba separarla. El resultado tenía 19 secciones y
nueve secundarios (cinco cuentas y cuatro instrucciones), sin ningún All In.
No era un filtro de reproducción ni una marca guardada perdida: biblioteca y
ajustes del proyecto seguían con el arreglo vacío.

La nueva inferencia completa con proveedor 8, sin caché y con red bloqueada,
tardó **55,70 s** y produjo **28 fragmentos**, conservando las **19 secciones**
y sus entradas. Las pausas reales de 200/201 ms permiten reconocer All In
independientemente a **50,71 / 106,57 / 189,33 s**. El borrador tiene ahora
**12 secundarios: cinco cuentas y siete instrucciones**; todas las marcas
anteriores conservan nombres y posiciones. Estas indicaciones mantienen el
instante hablado y no heredan la anticipación del Interludio.

App/probes compilados y **7/7 suites en 10,41 s**. Una nueva prueba del
componente nativo cubre el intervalo 35–62 s, etiqueta/punto de Toda la banda,
selección, reproducción, confirmación mediante la transacción habitual,
anclaje persistido y un solo Deshacer. La captura usa una onda de prueba y
no representa una confirmación en la canción del usuario. La lectura real de
caché conserva exactamente los nuevos anuncios y puntos. La interpretación
del corpus ASR anterior permanece en 121/121 (sin nueva inferencia del corpus).
`output/vi-la-luz-secondary-20261104/` conserva el antes/después, verificación,
logs y `all-in-native.png`. No se reinició la app ni se modificó el arreglo
del usuario durante estas comprobaciones.

### Conjunto reproducible de escenarios

[GuideDetectionTestSet.md](GuideDetectionTestSet.md) documenta el set de 24
audios compuestos, sus referencias congeladas y el procedimiento de inferencia,
comparación y revisión humana. Complementa el corpus de palabras aisladas con
cuentas, anuncios consecutivos, idiomas mezclados, métricas, fronteras de
compás y controles sin voz. Su primer resultado fue 19/24 casos y 74/89 marcas;
los cinco casos con diferencias quedan como insumo para futuras correcciones.

La primera adaptación guiada por esos casos usa el proveedor 9: pausas reales
de 60 ms, reagrupación contigua y aceptación de una única descomposición
reconocida. La nueva inferencia completa conserva las referencias y el modelo:
**21/24 casos y 79/89 marcas**, cinco marcas recuperadas y cero regresiones.
Las dos cuentas rápidas y el compuesto sin pausa siguen revisables. El
comparador documentado en [GuideScenarioComparison.md](GuideScenarioComparison.md)
conserva sus omisiones y los tiempos antes/después por evento.
La inferencia nueva del corpus aislado conserva **121/121 interpretados**
(112 directos), sin omisiones ni extras. Su cobertura de voces es 17 familias,
21/26 utilidades y cuentas 1–7; los faltantes están declarados en el contrato
del conjunto. No equivale a resolver los tres casos de tiempo pendientes.

El cierre posterior con proveedor 10 y evidencia de voces conocidas sí resuelve
los tres casos temporales: **24/24 casos originales, 89/89 marcas**. La nueva
matriz rápida añade **30/30 casos y 116/116 marcas**, a 120–180 BPM y pausas de
200 a 0 ms, incluidos solapamientos. Total: **54/54 y 205/205**, tolerancia
±40 ms sin cambios, máximo observado 17,125 ms, cero extras y regresiones.
App/probes compilados y 8/8 suites nativas aprobadas. MI TESTIMONIO y VI LA LUZ
se reanalizaron completas; sus cinco cuentas iniciales por guía conservan
1, 2, 2, 3, 4 dentro de ±40 ms frente a correlación independiente, sin cambiar
fronteras principales ni arreglos del usuario. Véase
[GuideRapidRecognition.md](GuideRapidRecognition.md) para procedencia, revisión
humana del informe anterior, reproducción y límites de las voces conocidas.

### Histórico: Glorioso Día y contrato 8 de entrada musical — 4 de noviembre

La regla de indicaciones al compás siguiente descrita en este apartado y en
su replay de contrato 2 fue reemplazada el 5 de noviembre por
`sections-mvp-9-secondary-spoken-timing`. Se conserva la evidencia anterior;
sus resultados no describen el tiempo activo de las indicaciones. El
refinamiento de fase y la regla de las principales sí continúan vigentes.

El arreglo guardado de Glorioso Día (110 BPM, 4/4) permitió reproducir los
19 destinos principales con el origen inicial ASR de 0,020 s. Los inicios
hablados estaban entre 21,818 y 40,000 ms antes de la frontera más cercana,
fuera de la tolerancia de 20 ms; por ello la propuesta quedaba en esa misma
frontera. Guía Dyn anticipaba después un compás desde esa entrada ya
adelantada. Las seis cuentas mantenían sus tiempos correctos.

El refinamiento de fase inicial conservó el margen de frontera de 20 ms y usó
consenso temporal de las cuentas, sin moverlas. En este contrato histórico,
las cuatro indicaciones reconocidas recibieron un destino musical posterior
independiente del instante hablado; seguían siendo cues sin dividir la canción. El scheduler
existente anuncia las principales una sola vez un compás antes de su entrada
y reproduce cada cue en su punto confirmado.

Las cifras históricas de 54/54 casos y 205/205 marcas describen la política
anterior, con origen suministrado por el manifiesto e instrucciones situadas
en el tiempo hablado. No acreditan por sí mismas este refinamiento del origen
en el flujo real ni el nuevo destino musical de las instrucciones. Las
referencias anteriores deben conservarse como evidencia de aquel contrato;
la validación del ajuste requiere referencias explícitas para las entradas
nuevas y para los casos ambiguos o sin consenso.

El probe admite `--require-cache-hit 1` para comprobar la alineación sin una
nueva inferencia. Esta opción exige una entrada válida de la caché y rechaza
su ausencia antes de llamar al decoder; no basta con comprobar `cacheHit`
después de ejecutar Whisper. El replay histórico de los 54 audios conservó
sus referencias anteriores y produjo una evaluación separada del contrato 2,
en el que 43 indicaciones usaban la entrada musical posterior. Ese replay
evaluó alineación a partir del ASR guardado; no acredita reconocimiento de voz
nuevo ni sustituye la evidencia histórica de inferencia con caché vacía.

#### Evidencia histórica del contrato 8 y replay de contrato 2

La compilación final de app y probes terminó con código 0
(`/tmp/pw-glorioso-musical-entry-final-build.log`),
la firma estricta del bundle se verificó y las **8/8 suites nativas** pasaron
en **11,88 s**. La comprobación nativa de Glorioso Día exigió caché válida
antes del decoder: **19/19 principales, 4/4 indicaciones y 6/6 cuentas**
coincidieron con referencias fijadas antes de ejecutar el probe. El consenso
refinó el origen de 0,020 s a 0 s; las seis posiciones de cuenta y el JSON ASR
permanecieron idénticos. No hubo escrituras al arreglo guardado. El informe,
salida nativa y verificación están en
`output/glorioso-dia-entry-fix-20261104/`.

El replay nativo de los dos conjuntos conservó **54/54 casos, 205/205 marcas
y cero extras** bajo aquel contrato de entrada musical. Las 43 referencias
de instrucciones se congelaron en su nueva versión antes de ejecutar; las
**77 cuentas y 85 principales** conservan exactamente sus eventos y posiciones
anteriores con los orígenes explícitos de cada escenario. Se reutilizó la
caché del proveedor 10 y no se ejecutó ASR nuevo. Esta comprobación de
alineación se conserva separada de las evaluaciones históricas en
`output/guide-musical-entry-20261104/Informe.md` y en
`event-preservation-verification.json` de esa carpeta.

La captura `secondary-entry-native.png` de la comprobación del componente
nativo usa una fixture de VI LA LUZ para mostrar la indicación en su entrada
propuesta. No representa una confirmación ni un guardado de Glorioso Día
en la app del usuario.

### Histórico: tiempo hablado y reparaciones locales de ALABA — contrato 9

Las reparaciones de texto específicas de intensidad descritas en este
apartado fueron retiradas en el contrato 10 de catálogo cerrado. Se conservan
el diagnóstico y sus resultados como evidencia histórica; no describen la
resolución automática activa de esas frases. La política temporal de
indicaciones al inicio hablado sí sigue vigente.

En ALABA (127 BPM), Drums In tenía un inicio ASR de **191,800 s**. La regla
del contrato 8 proponía **192,755906 s**, un retraso de **0,955906 s**, cercano
a dos negras. El anuncio `Submit intensity.` comenzaba en **195,580 s**, pero
quedaba como una principal desconocida con entrada alrededor de **196,535 s**.

El contrato 9 mantiene Drums In exactamente en 191,800 s y clasifica el anuncio
completo de intensidad como Build, una indicación secundaria en 195,580 s.
También reconoce `Summit intensity` como la misma corrección semántica. La
transcripción ASR original y su evidencia no cambian. Las principales
conservan el compás siguiente y la tolerancia de frontera de 20 ms; el
refinamiento de origen de Glorioso Día y los tiempos de cuentas se conservan.

La compilación final de app y probes terminó con código 0 en
`/tmp/pw-alaba-secondary-timing-build.log`; la firma estricta se verificó y
las **8/8 suites nativas** pasaron en **12,39 s**, según
`/tmp/pw-alaba-secondary-timing-ctest.log`.

La comprobación nativa de ALABA usó sólo la caché válida, sin ejecutar el
decoder. Las **27 principales restantes** conservaron sus nombres e inicios;
las **siete indicaciones** coincidieron exactamente con sus onsets hablados.
`Submit intensity.` dejó de crear la principal desconocida número 28 y pasó
a Build en 195,580 s. Se preservaron identidad y posición de los **tres
puntos de cuenta** reales: uno permanece como el compuesto literal
`uno, dos, tres, cuatro.`. Esta corrección no inventa tiempos de cuentas
individuales para ese compuesto. El ASR crudo, los archivos de fuente y el
arreglo guardado permanecieron intactos. La evidencia está en
`output/alaba-secondary-fix-20261105/Informe.md` y `verification.json` de esa
carpeta.

El replay de alineación desde las transcripciones congeladas del proveedor 10
alcanzó **54/54 casos, 205/205 marcas y cero extras**. Esta vez las **205
referencias y los eventos reales** coinciden exactamente con los originales
de `run-003` y `run-010`: 77 cuentas, 85 principales y 43 indicaciones en su
tiempo hablado, sin referencias nuevas ni modificadas. No se ejecutó ASR
nuevo ni se alteraron los WAV, JSON o cachés originales. Los orígenes
explícitos de los escenarios siguen separados del ajuste inicial del editor.
El informe es `output/guide-secondary-spoken-20261105/Informe.md`.

Estas comprobaciones no confirman sugerencias ni corrigen automáticamente
los datos ya guardados. La adaptación se aplica al generar una nueva revisión
y requiere su confirmación habitual. Tampoco acreditan nueva escucha humana
ni una nueva ejecución de la app. Los resultados del contrato 8 permanecen
como evidencia histórica separada.

### Incremento de catálogo cerrado — 5 de noviembre

`sections-mvp-10-catalogue-review` y el proveedor 11 separan evidencia
automática y elección explícita. Unknowns y compuestos aún no resueltos no
añaden principales desconocidas; las opciones débiles o condicionadas por
gramática tampoco autorizan esa conversión. El selector usa el catálogo
compartido completo y conserva la evidencia original. Su implementación y
límites se documentan en [CatalogueRecognition.md](CatalogueRecognition.md).

La validación nueva del proveedor 11 aprobó 8/8 suites nativas, 54/54
escenarios con 205/205 marcas y cero pendientes, y 121/121 anuncios del
corpus conocido. ALABA conserva una cuenta agrupada pendiente y recupera
Build por evidencia física. El control ajeno al catálogo permanece pendiente
sin una principal arbitraria. Detalles y límites están en el documento
enlazado y en `output/closed-catalogue-20261105/Informe.md`.
Se compiló y verificó la firma local; no se reinició la app ni se modificaron
arreglos guardados. La revisión y confirmación de canciones no quedan
acreditadas por esas comprobaciones.
