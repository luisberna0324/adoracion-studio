---
type: Test Contract
title: Conjunto reproducible de detección de secciones
description: Audios controlados, referencias temporales y revisión humana de la guía
resource: tests/fixtures/guide-scenarios/suite.json
timestamp: 2026-11-04T00:00:00-03:00
---

# Conjunto de pruebas de la guía

Este conjunto convierte los problemas observados en «MI TESTIMONIO» y «VI LA
LUZ» en **24 escenarios reproducibles, con 90 anuncios de referencia**. Produce
audios nuevos a partir de las grabaciones distribuidas con PlayWorship; no usa
las canciones privadas del usuario ni genera voces mediante un servicio externo.
Los resultados esperados se fijan antes de ejecutar Whisper. Un fallo sirve
para preparar una corrección de código y comprobarla después sobre el mismo
audio, sin cambiar la referencia para que el informe quede aprobado.

El contrato de transcripción, interpretación y alineación sigue definido en
[SectionDetection.md](SectionDetection.md). Este conjunto comprueba ese
contrato; no crea otra implementación del arreglo o del reproductor.

El [guion de casos de uso](../../../tests/fixtures/guide-scenarios/use-cases.md)
expone las cuentas, Intro y anuncios compuestos con sus tiempos hablados y
marcas esperadas. [GuideScenarioComparison.md](GuideScenarioComparison.md)
documenta cómo comparar el reconocimiento antes y después de una corrección,
con referencias y modelo idénticos, mediante texto y CSV por evento.

## Archivos que se conservan

- `tests/fixtures/guide-scenarios/suite.json`: especificación versionada y
  revisable de los escenarios, idiomas, referencias, BPM, métrica y origen.
- `assets/guides/guides-en-es.zip`: grabaciones originales, abiertas para
  lectura. Su SHA-256 queda fijado en la especificación.
- El manifiesto del corpus aislado identifica cada fuente por contenido,
  familia, ordinal e idioma. El generador de escenarios utiliza esas fuentes
  y comprueba su procedencia; no sustituye silenciosamente un idioma ausente.
- El directorio generado contiene WAV, plan/manifiesto, posiciones en frames
  y huellas. Los resultados de cada ejecución se escriben en un directorio
  independiente con JSON crudo de voz y evaluación.
- El informe HTML permite escuchar, comparar esperado/obtenido y registrar
  observaciones. Los datos exportables permiten revisar los mismos resultados
  en JSON/CSV sin depender de la pantalla del reproductor.

Los audios y resultados voluminosos viven en `output/`, fuera del seguimiento
de Git. La especificación y este contrato permanecen junto al código para que
otro desarrollador pueda reproducir el experimento con el mismo pack.

## Escenarios incluidos

| ID | Qué se verifica |
| --- | --- |
| `catalogo-es-a` | Intro, Verso 1, Pre-Coro 1 y Coro 1 en español. |
| `catalogo-en-b` | Refrain, Post-Chorus, Bridge 1 e Instrumental en inglés. |
| `catalogo-es-c` | Solo, Interludio, Repetir/Tag y Vamp en español. |
| `catalogo-mixto-d` | Salida, Final, Turnaround, Exhortación y Rap. Completa las 17 familias; Turnaround utiliza la voz EN existente. |
| `interludio-all-in-200` | Interludio y Toda la banda con 200 ms de pausa entre voces. |
| `interludio-all-in-060` | El mismo compuesto con una pausa de 60 ms. |
| `interludio-all-in-000` | El mismo compuesto sin pausa añadida. |
| `cuenta-es-97` | Cuenta 1, 2, Intro, 2, 3, 4 a 97 BPM. |
| `cuenta-es-116` | Cuenta rápida en español a 116 BPM, con posibles colas contiguas. |
| `cuenta-en-120` | Cuenta en inglés a 120 BPM, con solapamiento controlado de voces. |
| `ordinales-y-intro` | Ordinales realmente pronunciados y dos Intro que permanecen sin número. |
| `indicaciones-consecutivas` | Subir intensidad, Toda la banda y Entra batería como tres puntos, sin nuevas secciones. |
| `idiomas-alternados` | Español e inglés dentro de la misma guía, con principales y secundarias. |
| `repetidos-sin-ordinal` | Intro/Interludio sin números artificiales y convención visual de Refrain repetido. |
| `tempo-69-origen` | Compases a 69 BPM, 4/4 y origen a 1,25 s. |
| `metrica-90-3-4` | Compases de tres negras, a 90 BPM y origen a 0,375 s. |
| `metrica-116-6-8` | Seis corcheas por compás, a 116 BPM y origen a 0,75 s. |
| `final-sin-compas` | Final anunciado sin espacio para su siguiente compás; exige un error de alineación explícito. |
| `silencio-sin-voz` | Ausencia de anuncios, cuentas e instrucciones. |
| `click-sin-voz` | Pulsos de click que no deben convertirse en voz reconocida. |
| `guia-volumen-bajo` | Guía con ganancia reducida 18 dB y referencias sin modificar. |
| `guia-ruido-suave` | Ruido sintético suave con semilla fija, manteniendo la referencia de voz. |
| `frontera-15ms` | Inicio hablado dentro de la tolerancia de 20 ms antes de una línea. |
| `frontera-35ms` | Inicio fuera de esa tolerancia, con un compás de destino diferente. |

Los escenarios de 60/0 ms y solapamiento son deliberadamente exigentes. Su
referencia continúa pidiendo todos los anuncios incluidos en el audio. Si la
aplicación conserva un compuesto para revisión, eso demuestra que no ocultó
su evidencia, pero **no cuenta como haber reconocido todas sus marcas**.

## Construcción y tiempos de referencia

La colocación de un anuncio se resuelve en este orden:

1. `onsetSeconds`, cuando se especifica un instante absoluto.
2. El final de voz activa del anuncio anterior más `gapSeconds`.
3. `originSeconds + bar × duraciónDelCompás + floatOffsetSeconds`, para un
   compás interno basado en cero. El desplazamiento habitual es 0,12 s.

La duración de un pulso de la métrica es `(60 / BPM) × 4 / denominador`;
la duración del compás multiplica ese valor por el numerador. El origen es
una entrada explícita de la prueba, independiente del primer tiempo reconocido
por Whisper. El probe recibe ese origen mediante `--grid-origin`.

Las fuentes del pack duran aproximadamente 1,25–1,28 s, aunque muchas voces
ocupan sólo 0,4–0,9 s. Por eso una pausa nominal de 60 ms no puede construirse
uniendo archivos completos: se añadiría su silencio oculto y se dejaría de
probar el caso previsto. El generador mide actividad, recorta exclusivamente
las copias de trabajo y registra los frames conservados y cualquier margen.
`gapSeconds` se refiere a **voz activa**, y el manifiesto debe expresar el
instante activo finalmente compuesto. Los originales permanecen intactos.

Las cuentas se colocan sobre instantes conocidos: primero 1 y 2 separados
por dos negras, después Intro y las cuentas 2, 3, 4 en negras consecutivas.
Sólo se incluyen esas cinco cuentas. Las grabaciones pueden tener colas que
se toquen o solapen: el manifiesto conserva ese hecho, sin cortar palabras ni
alterar los instantes esperados para facilitar el reconocimiento.

Los efectos son explícitos: ganancia global, ruido de RMS indicado y semilla
determinista, o click de control sin voz. La referencia semántica y musical
no cambia por añadirlos. Ruido sintético y voces reutilizadas no representan
una mezcla musical real ni una voz nueva.

## Cómo evaluar un resultado

Se comparan tres planos separados:

1. **Voz:** texto literal, idioma, alternativas, confianza e intervalos de
   palabra obtenidos por el proveedor real. La referencia nunca condiciona
   la inferencia ni se introduce en su prompt.
2. **Significado:** sección, instrucción o cuenta; clave canónica; ordinal
   realmente hablado; propuesta pendiente de revisión o equivalencia aplicada.
3. **Tiempo:** inicio hablado de referencia y posición musical propuesta.
   Una principal debe ir al compás siguiente, con la tolerancia contractual de
   frontera; una instrucción conserva su instante hablado. Una cuenta
   recuperada mediante intervalos se contrasta por su posición propuesta, no
   por el límite inferior aproximado de su intervalo ASR.

Cada evento obtenido puede corresponder a **un solo** evento de referencia.
Esto es necesario para distinguir los dos «2» de la cuenta. La presencia del
nombre en cualquier lugar de la transcripción no acredita un acierto; tampoco
lo hace una familia correcta situada un compás antes o después. El informe
debe mostrar omisiones, extras, clase/ordinal incorrectos y diferencias de
tiempo, junto con la tolerancia usada en esa ejecución.

`requireUnnumbered` exige que no se añada un número a la etiqueta. Refrain
repetido conserva la convención visual actual de apariciones; ese número de
aparición no se confunde con un ordinal pronunciado en el audio.
Cuando la referencia declara `ordinal`, el evaluador exige ese número; cuando
lo omite, no exige una aparición concreta. Con el JSON actual no siempre se
puede distinguir un ordinal inventado por ASR de una numeración visual
legítima. Esa distinción requiere revisión humana o exponer el ordinal hablado
como metadato separado. Intro e Interludio sí tienen casos explícitos con
`requireUnnumbered`; no se afirma cobertura automática completa de esta
distinción para las otras familias.

En `final-sin-compas`, `expectReview` mantiene el anuncio en la fuente y en
la documentación, pero no exige crear una sección fuera del rango.
`expectedPolicy.alignmentError` requiere un error explícito: no se debe mover
el inicio hacia atrás para hacer caber la propuesta. Los controles negativos
esperan cero eventos y muestran cualquier marca adicional como falso positivo.

Las comparaciones con una ejecución anterior exigen el mismo audio y huellas,
la misma especificación y los mismos parámetros de cuadrícula. Un cambio de
modelo, proveedor, composición o referencias se registra como una condición
nueva; no se mezcla con la mejora de una corrección de interpretación.

## Ejecutar desde la raíz del repositorio

Primero debe existir el corpus de fuentes generado por
`scripts/build_guide_recognition_corpus.py`; su procedimiento está documentado
en [SectionDetection.md](SectionDetection.md). Con esas fuentes preparadas:

```sh
python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/suite.json \
  --corpus-manifest output/guide-recognition-corpus-20261104/manifest.json \
  --output-dir output/guide-scenarios-20261104 --dry-run

python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/suite.json \
  --corpus-manifest output/guide-recognition-corpus-20261104/manifest.json \
  --output-dir output/guide-scenarios-20261104 --execute

python3.12 -B scripts/run_guide_scenarios.py \
  --manifest output/guide-scenarios-20261104/suite-manifest.json \
  --probe build/whisper-macos/MTControlsGuideTranscriptionProbe_artefacts/Release/MTControlsGuideTranscriptionProbe \
  --model /ruta/local/ggml-base-q5_1.bin \
  --output-dir output/guide-scenarios-20261104/run-001

python3.12 -B scripts/report_guide_scenarios.py \
  --manifest output/guide-scenarios-20261104/suite-manifest.json \
  --evaluation output/guide-scenarios-20261104/run-001/evaluation.json \
  --output-dir output/guide-scenarios-20261104/report
```

En este Mac, `python3.12` puede reemplazarse por el runtime ya preparado:
`/Users/luisberna/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3.12`.
El modelo multilingüe preparado está en
`/Users/luisberna/Library/MTControls/models/ggml-base-q5_1.bin`.
Estos comandos no autorizan descargar otro modelo, instalar un motor ni
publicar resultados. Cada ejecución guarda su caché y sus resultados separados
de la biblioteca de canciones; no confirma ni modifica un arreglo del usuario.

El runner devuelve `0` cuando todos los casos pasan, `2` si finalizó con
diferencias y `1` ante errores de herramienta o especificación. Para volver a
evaluar los JSON existentes sin repetir Whisper se usa el mismo comando con
`--evaluate-only`; esa operación se identifica como evaluación de transcripciones
existentes, no como otra inferencia. Antes de cambiar el evaluador conviene
conservar su `evaluation.json` original con otro nombre.

## Convertir hallazgos en correcciones de código

1. Escuchar el caso completo y el intervalo señalado. Confirmar que la
   referencia y los frames del manifiesto describen el audio generado.
2. Registrar por separado un error de referencia, de reconocimiento, de
   interpretación, de tiempo o de presentación. Una frase mal compuesta no se
   arregla añadiendo un alias al motor.
3. Exportar la decisión humana con ID de caso/evento, huella del conjunto,
   significado o tiempo corregido y una nota. No marcar automáticamente las
   respuestas como validadas.
4. Aplicar una adaptación acotada en código y una regresión para el caso,
   conservando el texto ASR original y los casos negativos cercanos.
5. Ejecutar el mismo conjunto en otro directorio y comparar errores, omisiones,
   extras y desplazamientos. Mantener visibles los fallos que sigan pendientes.

El informe es **insumo de ingeniería**. Ni su HTML ni las decisiones exportadas
se importan automáticamente como reglas de producción, modifican el store de
aprendizaje local o confirman secciones en canciones. Una adaptación requiere
la revisión de código habitual y una comprobación posterior.

## Límites de la evidencia

Este conjunto usa grabaciones conocidas del pack. Las correcciones acústicas
confirmadas anteriormente pueden mejorar su resultado, de modo que la cifra
obtenida es una **regresión sobre esas voces**, no una estimación independiente
de precisión para todas las canciones. El corpus aislado de 121 fuentes sigue
siendo una comprobación complementaria de cobertura.

Los escenarios compuestos verifican reconocimiento y cálculo en archivos
controlados. No acreditan por sí solos los gestos, la visibilidad durante
reproducción, la confirmación ni el guardado/reapertura de la app: esas rutas
mantienen sus pruebas nativas de UI y su revisión manual propia.

## Primera ejecución observada — 4 de noviembre de 2026

Se generaron **24 WAV mono PCM16 a 16 kHz, 359,6884375 s** en total,
con las 17 familias principales. Los **90 anuncios** producen **89 marcas
esperadas**: el anuncio de Final restante comprueba el error explícito por no
existir un compás posterior dentro del audio.

Whisper ejecutó los 24 archivos con una caché nueva y red bloqueada, entre
`2026-11-05T00:02:40Z` y `00:05:39Z` (4 de noviembre por la noche en este Mac).
Todos los JSON indican `cacheHit=false`. Proveedor:
`1.9.5:guide-energy-sinc16k-beam5-prompt128-bilingual-8-compound-directives`.
Huella del modelo: `00dc52090d95552a31c5fdaff05c9c6bf233ce64070a8ffddd8e57177effa043`.

La evaluación estricta final reutilizó esos JSON y verificó huellas, BPM,
métrica, origen, duración y el error específico del caso tardío. Resultado:
**19 casos aprobados, 5 con diferencias; 74/89 marcas coincidentes, 15
ausentes y 5 compuestos adicionales pendientes de revisión**.

| Caso con diferencias | Evidencia y siguiente corrección a investigar |
| --- | --- |
| `interludio-all-in-060` | Interludio y Toda la banda quedan unidos; faltan ambas marcas independientes. |
| `interludio-all-in-000` | El mismo fallo sin pausa entre voces activas. |
| `cuenta-es-116` | Reconoce las dos primeras cuentas; Intro y las tres cuentas siguientes quedan en un compuesto. |
| `cuenta-en-120` | El mismo patrón en inglés con solapamiento controlado. |
| `indicaciones-consecutivas` | Subir intensidad, Toda la banda y Entra batería quedan unidos en una propuesta revisable. |

Los casos de catálogo, pausa de 200 ms, cuenta a 97 BPM, idiomas alternados,
origen desplazado, 3/4, 6/8, frontera de 15/35 ms, volumen reducido, ruido,
silencio, click y Final fuera de rango pasaron. Se verificó un plan regenerado
idéntico, los hashes y frames de los 24 WAV y los originales sin cambios.
El control genérico de medios avisa sobre formato de podcast y silencio; estos
WAV son entradas de ASR a 16 kHz y el silencio digital es un control negativo
deliberado. No se normalizaron para alterar esas condiciones.

Evidencia local en `output/guide-scenarios-20261104/`:

- `suite-manifest.json`, `references.csv`, `plan.json` y `assembly-verification.json`.
- `run-001/transcripts/` y `run-001/logs/`: salida cruda y diagnóstico por audio.
- `run-001/evaluation-asr-run.json`: evaluación conservada de la inferencia nueva.
- `run-001/evaluation.json`: evaluación estricta posterior, sin repetir ASR.
- `report/guide-scenarios-report.html`, `report/Informe.md` y CSV de comprobaciones.

Esta primera ejecución comenzó antes de añadir los sidecars de procedencia al
runner. La evaluación la identifica como `legacy-context-verified`: verifica
el contexto del JSON y el WAV, sin inventar un comando retrospectivo. Las
siguientes ejecuciones guardan también comandos y huellas vinculadas en sus
sidecars. El informe se comprobó visualmente, reprodujo un audio y conservó una
nota de prueba tras recargar; esa nota se retiró al terminar. Todas las decisiones
humanas se entregan sin validar, para que la escucha y corrección sean reales.

## Primera mejora del reconocimiento — proveedor 9

Se corrigió la separación de compuestos explícitos con pausas de 60 ms y la
reagrupación de pausas internas de una indicación. Cada grupo vuelve a pasar
por Whisper y sólo se acepta una descomposición completa semánticamente válida;
la ambigüedad o el agotamiento del presupuesto conservan la frase revisable.
También se agrupan subtokens antes de exigir un intervalo positivo por palabra,
y la pasada temporal conserva el idioma del padre. Los tiempos del arreglo y
las referencias de la prueba permanecen iguales.

La inferencia nueva de los 24 audios terminó entre `00:46:05Z` y `00:49:21Z`
del 5 de noviembre (4 por la noche local), con el mismo modelo, caché vacía y
red bloqueada. Los 24 resultados tienen `cacheHit=false` y procedencia
`input-and-transcript-verified`. El evaluador coincide byte por byte con el
snapshot usado para la evaluación estricta inicial; su SHA-256 es
`cfd0d6e74c6527df480a933cf183089480a03e05b98045d4c0f04149746779cd`.

| Métrica | Proveedor 8 | Proveedor 9 |
| --- | ---: | ---: |
| Casos aprobados | 19/24 | 21/24 |
| Marcas correctas | 74/89 | 79/89 |
| Marcas ausentes | 15 | 10 |
| Compuestos adicionales revisables | 5 | 3 |

El comparador acredita **cinco marcas recuperadas y cero regresiones**:
`interludio-all-in-060` pasa de 0/2 a 2/2 e
`indicaciones-consecutivas` de 0/3 a 3/3. Sus posiciones coinciden con la
referencia dentro de la tolerancia, sin cambiar el instante hablado de las
indicaciones ni el compás siguiente para la principal.

Permanecen con diferencias `interludio-all-in-000`, `cuenta-es-116` y
`cuenta-en-120`. El primero no tiene una pausa acústica suficiente. En los
dos conteos, Whisper reconoce el texto completo, pero asigna tiempos iguales
al final a varias palabras numéricas. No hay intervalos positivos que permitan
proponer cada número sin inventar su instante. Los experimentos auxiliares de
segmentación por palabra y DTW no aportaron evidencia suficiente para integrar
otra recuperación; el ensayo de contexto silencioso adicional tampoco mejoró
los resultados al probarlo en el proveedor nativo. Los parámetros
experimentales se retiraron; el proveedor final no activa DTW ni añade ese
contexto. Los tres fallos permanecen visibles en los informes.

App de escritorio, probes y tests compilados; **7/7 suites nativas** pasaron
en 10,83 s. El selector/segmentador suma **114 comprobaciones**, incluidas
particiones ambiguas, presupuesto agotado tras una candidata válida,
cancelación, la pausa interna de All In y tiempos por palabra fuera de rango.
La firma del bundle local se verificó. Esta validación no reinicia la app
abierta ni acredita una nueva confirmación o guardado de canciones.

Evidencia adicional en el mismo directorio generado:

- `run-002/`: nueva inferencia, evaluación, logs y sidecars de procedencia.
- `report-002/`: informe con audio y revisión por evento.
- `comparison-001-002/`: comparación textual completa de las 89 referencias,
  CSV por caso/evento y JSON antes/después.
- `diagnostics/`: experimentos acotados; no se mezclan con la inferencia final.

La comparación y su código de salida 0 acreditan ausencia de regresiones en
este conjunto, no que los 24 casos estén resueltos. El siguiente incremento
debe aportar evidencia temporal nueva para las cuentas y los anuncios sin
pausa, conservando estos WAV y sus fallos como regresiones abiertas.

La regresión amplia del catálogo también ejecutó ASR nuevo con proveedor 9,
mismo modelo, caché vacía y red bloqueada: **112/121 coincidencias directas y
121/121 interpretadas**, en 209,98 s. No hubo omisiones, divisiones, propuestas
tentativas ni segmentos extra. Conserva la cobertura anterior de las voces
aisladas, incluidas las reparaciones confirmadas existentes; no resuelve los
tres escenarios temporales pendientes por tener ese catálogo aprobado.

Ese corpus contiene voces de las 17 familias principales, **21 de las 26
utilidades del vocabulario** y cuentas del 1 al 7 en ambos idiomas. No hay
grabaciones de All, Big, Slowly, Key y Last en ninguno de los dos idiomas;
faltan además Turnaround ES, Slowly Build ES, Guitar EN, Ad Lib ES y
Worship Freely ES. Estos faltantes se declaran, sin sustituir un idioma ni
atribuirles cobertura acústica. `catalogue-regression-009/` conserva su informe,
JSON crudo, evaluación nativa, CSV, comandos y huellas antes/después.

## Cierre de los escenarios temporales — proveedor 10

La recuperación con evidencia de las grabaciones conocidas conserva el texto
Whisper y resuelve los tres pendientes del conjunto original: **24/24 casos,
89/89 marcas, cero extras**. Frente al proveedor 9 se recuperan diez eventos
sin regresiones, usando los mismos WAV, referencias, modelo y evaluador.
`run-003/`, `report-003/` y `comparison-002-003/` conservan esa inferencia nueva.

El conjunto adicional de **30 combinaciones rápidas** alcanza **30/30 casos y
116/116 marcas**. Entre ambos conjuntos: **54/54 casos, 205/205 marcas** dentro
de ±40 ms, sin extras; mayor error observado 17,125 ms. Las ocho suites nativas
pasaron. [GuideRapidRecognition.md](GuideRapidRecognition.md) documenta la
matriz de 120–180 BPM, pausas de hasta 0 ms, solapamientos, resultados reales,
revisión humana recibida y límites de cobertura. Estos resultados posteriores
sustituyen los pendientes históricos para esas mismas referencias, sin borrar
la evidencia anterior ni convertir la validación en precisión universal.
