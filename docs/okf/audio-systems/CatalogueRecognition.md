---
type: Analysis and Review Contract
title: Reconocimiento y revisión desde el catálogo de marcas
resource: src/analysis/GuideSectionInterpretation.h
timestamp: 2026-11-06
---

# Catálogo de reconocimiento y revisión

La detección usa un conjunto compartido de marcas y sus roles. El texto de
Whisper y la evidencia de audio permanecen separados de la elección musical.
Un anuncio sin resolución suficiente se revisa como punto en la onda; no se
convierte en una sección desconocida que divida el arreglo.

## Una fuente de nombres y roles

`guideCatalogEntries()` deriva sus opciones de `GuideVocabulary.h`: las 17
familias principales, sus ordinales permitidos por `numberedVariantLimit`,
las 26 utilidades, las utilidades literales declaradas y los conteos 1–7.
Cada opción contiene `kind`, `type`, `ordinal`, `key` y `label`. La identidad
canónica incluye el rol: una indicación o cuenta no es una sección musical.
El catálogo y sus variantes no se reconstruyen mediante una lista especial
de nombres para cada canción ni mediante el orden esperado de sus anuncios.

`guideCatalogChoices(segment, learnedMappings)` devuelve todas las opciones,
ordenadas por evidencia para facilitar la revisión. `score` es una prioridad
de presentación, no una probabilidad calibrada. `supported` distingue una
hipótesis con soporte suficiente de una mera posibilidad para el usuario.
El selector conserva el catálogo completo aunque no exista una coincidencia
preferida; los faltantes de grabaciones del pack no crean audio nuevo.

## Evidencia tipada después del ASR

`interpretGuideAnnouncement` clasifica cuentas, indicaciones y principales
con la misma ruta. También considera las alternativas ordinarias de ambos
idiomas sin modificar `text`, `language`, tiempos, confianza, palabras ni
`alternatives` del segmento original. Una alternativa apta debe usar `es` o
`en`, valores finitos, `avgLogProbability` entre −1,2 y 0 y
`noSpeechProbability` entre 0 y 0,4. Para respaldar una interpretación
automática no puede quedar más de 0,2 por debajo de la puntuación del texto
seleccionado. Significados o roles incompatibles conservan la ambigüedad.
Una corrección local de familia no puede anular una alternativa plausible de
indicación o cuenta para fabricar una principal.

Alternativas plausibles más débiles y similitud de texto pueden ordenar el
selector, con `supported=false`; no crean por sí solas una marca aceptable.
Las correcciones específicas de intensidad ensayadas en el contrato 9 fueron
retiradas. Su caso se resuelve por estas alternativas tipadas, evidencia
acústica independiente o elección explícita del catálogo. Las correcciones
históricas que siguen vigentes mantienen su función de interpretación de
anuncios completos; no reescriben el padre ASR.

## Voz conocida y gramática acotada

La recuperación y la alineación consultan `recognitionState()`. Un anuncio
atómico sin resolver (`unresolved`) o con una familia tentativa (`tentative`)
puede buscar evidencia física y, si hace falta, alternativas de gramática.
Una marca válida respaldada (`resolved`) pasa a alineación. Los conflictos
de significado, ordinales fuera del catálogo y casos de revisión obligatoria
(`reviewOnly`) quedan para el editor. Los compuestos disponen de su propia
ruta para localizar las partes en el audio.

El proveedor de voz es
`1.9.5:guide-energy-sinc16k-beam5-prompt128-bilingual-13-guide-repetitions`,
con sufijo `:pack-<SHA256>` o `:pack-unavailable`. La identidad nueva separa
sus resultados de las cachés anteriores; no implica otro modelo ni permite
atribuir los resultados antiguos a esta pasada.

Para un anuncio atómico aún no resuelto, `matchKnownGuideAtomic` compara el
PCM contra los significados grabados del pack completo. Una aceptación
aporta un único hijo en `acousticAnnouncements`: exige correlación
normalizada de al menos 0,94, residuo de reconstrucción de hasta 0,12 y un
margen de al menos 0,08 frente a otro significado competidor. El rango fuente
y los metadatos se validan, y el padre permanece literal. El catálogo físico
no invalida una alternativa ya respaldada ni consume una parte de un anuncio
compuesto para presentarla como si fuera la frase completa. Se mantienen los
guards, particiones y pruebas físicas exigidos para los compuestos.

Si esa evidencia no resuelve el anuncio, se admite un reintento acotado con
`GuideCatalogueGrammar`, generado por `guideCatalogueUtterances()` desde el
mismo vocabulario ES/EN y sus variantes. No incorpora nombres de canciones,
tiempos, secuencias esperadas ni respuestas de los casos de prueba. Sus
resultados se conservan separados en `catalogueAlternatives`, nunca en lugar
de las alternativas ordinarias. Sus puntuaciones están condicionadas por la
gramática: siempre son opciones de revisión con `supported=false`, incluso
si su texto coincide con una marca válida. No demuestran que esa voz esté
presente en el audio.

## Repeticiones dentro de la guía

El proveedor 13 recoge ejemplos de indicaciones del catálogo reconocidas con
evidencia ordinaria de Whisper en la primera pasada. Si quedan anuncios elegibles,
vuelve a leer esas ventanas de la guía y compara el audio en mono a 16 kHz.
Las referencias pertenecen a la misma grabación. Sólo se consideran pendientes
o familias tentativas y compuestos con una sección y una indicación. Las cuentas
y los conflictos de significado siguen su ruta de revisión específica.

Una coincidencia exige similitud de la frase completa, de su comienzo y de su
final a la misma posición, además de una comprobación en la señal sin filtrar.
Otra instrucción o posición con puntuación cercana impide asignar la marca.
Si queda voz a un lado de la repetición, el mismo contexto de Whisper reconoce
únicamente ese recorte. La sección requiere una lectura literal del catálogo y
apoyo de alguna lectura del anuncio original. Si quedan palabras sin explicar,
la onda conserva el punto para elegir una marca.

En el JSON, `repetitionAnnouncements` contiene la indicación con índice de su
referencia original, correlación y tiempos medidos. En un compuesto contiene
también la sección reconocida en el recorte. El padre conserva el texto y las
alternativas de Whisper. El borrador conserva las reglas de posición por rol
y da prioridad a las elecciones explícitas del usuario. Se reutilizan el modelo
descargado y el contexto de inferencia de la primera pasada.

## Elegir sin crear una sección desconocida

El contrato de alineación `sections-mvp-10-catalogue-review` añade
`GuideAlignmentDraft::pendingAnnouncements`. Un desconocido, compuesto sin
resolver, rol ambiguo o candidato tentativo queda como
`GuidePendingAnnouncement`: ID, texto original, rango fuente y candidatos.
La UI dibuja **Elegir marca** en el tiempo original y abre el selector del
catálogo. Muestra hasta cinco posibles coincidencias con evidencia y las
familias completas agrupadas en Secciones, Indicaciones y Conteos.

`chooseGuideCatalogMark(draft, id, choice, error)` valida que la opción
pertenezca al catálogo y resuelve el rol de forma explícita. Una principal
usa el compás siguiente y la tolerancia de frontera de 20 ms; una indicación
o cuenta conserva su instante fuente. El ID y la evidencia original se
conservan. `GuideAlignmentOptions::catalogueSelections` recuerda las
elecciones por ID para reconstruir el origen; las correcciones visuales de
posición, nombres y descartes siguen reaplicándose mediante sus identidades.
`discardGuidePendingAnnouncement` retira únicamente el pendiente elegido.

Confirmar permanece bloqueado mientras haya pendientes o incidencias de
tiempo. Seleccionar o descartar permite terminar la revisión; no confirma el
arreglo por sí mismo. La aceptación sigue siendo una transacción aditiva
nativa con Deshacer y preservación de secciones/cues existentes. Reabrir la
app o cargar una canción no borra ni reinterpreta automáticamente lo guardado.

## Evidencia y límites

Validación del 5 de noviembre de 2026, con el proveedor 11:

- Compilación nativa y firma local válidas; 8/8 suites aprobadas en 26,35 s.
  Cubren roles, alternativas débiles, gramática no promocionable, evidencia
  física, elección por ID, cambio de origen, bloqueo y aceptación atómica.
- Inferencia nueva sin aciertos de caché sobre los 54 escenarios originales:
  205/205 marcas, cero extras y cero pendientes. Se mantuvieron WAV,
  referencias y tolerancia de ±40 ms; error máximo de 17,125 ms.
- Inferencia nueva del corpus: 121/121 anuncios interpretados, 112 directos
  y nueve mediante equivalencias previamente confirmadas. Son grabaciones
  conocidas disponibles; no demuestra precisión universal con otras voces.
  El corpus incluye 21 de las 26 indicaciones base: faltan grabaciones de
  All, Big, Slowly, Key y Last. El selector completo sí ofrece las 93 opciones.
- ALABA: el padre literal `Submit intensity.` se conserva. La comparación
  física recupera Build a 195,590563 s, correlación 0,99938, sin una nueva
  equivalencia específica para esa frase. Hay 36 marcas resueltas y una
  cuenta agrupada pendiente porque no hay tiempos individuales fiables.
  El pendiente ofrece los números 1–4 y acceso al catálogo completo.
- Un control hablado ajeno al catálogo produjo cero marcas automáticas y
  un punto pendiente con las 93 opciones; no se asignó una principal arbitraria.
- La captura del componente nativo acredita el punto Elegir marca, la regla
  habitual y Confirmar bloqueado. No acredita interacción con la canción real.

La evidencia, huellas y reportes están en
`output/closed-catalogue-20261105/Informe.md` y
`output/closed-catalogue-20261105/verification-final.json`.
La app compilada no se reinició y no se modificaron arreglos guardados.
La revisión en la app y la confirmación de canciones siguen siendo pruebas
separadas. Los resultados históricos conservan sus proveedores documentados.

## Reevaluación del 6 de noviembre

El proveedor 12 procesó de nuevo las 15 guías descargadas (14 audios distintos).
La alineación `sections-mvp-11-count-word-boundaries` conservó las 448 marcas
anteriores y ubicó los cuatro números de tres cuentas agrupadas, en **ALABA**,
**Tu Eres Mi Todo** y **Has Cambiado / Un Adorador**. Quedaron 28 anuncios
pendientes con el catálogo compartido. Al aplicar las tres correcciones del
perfil sobre esos mismos segmentos, quedaron 473 marcas y 15 pendientes en
seis canciones. Los 54 escenarios con ASR congelado conservaron las 205
marcas esperadas. El detalle por canción y los fragmentos para escuchar están
en `output/guide-states-counts-20261106/Informe.md` y `Pendientes.md` del mismo directorio.

La comprobación rítmica separada usa solo la guía y la pista de clic de cada
canción. En dos ventanas por guía, el clic coincide con el BPM guardado o su
subdivisión en 13 de 15 entradas. Los grupos largos de **In Jesus Name** y
**Forever Reign** aportan cuatro pulsos candidatos apoyados por ataques de voz.
`output/guide-states-counts-20261106/Tempo.md` registra los tiempos para
revisión. Esta medición es diagnóstica. La app sigue usando el BPM y el mapa
de tempo del proyecto para generar la cuadrícula.

## Verificación de las repeticiones con el proveedor 13

Se procesaron los 14 audios originales con cachés nuevas y se compararon las
15 entradas de biblioteca. Con las tres correcciones del perfil, las marcas
pasaron de 473 a 476 y los anuncios pendientes de 15 a 13. Con el catálogo
compartido pasaron de 460 a 463 y de 28 a 26, respectivamente. Se verificaron
los tiempos originales de Whisper, las cuentas, la cuadrícula y los nombres,
roles e inicios de las marcas preexistentes en ambas configuraciones.

En «Todo tu lo haces bien» se recuperó Instrumental en el compás 87 y las
indicaciones Sube intensidad a 175,390 s y Baja intensidad a 183,586 s. El
Puente 2 termina al comenzar Instrumental. Las otras 14 entradas conservaron
sus resultados anteriores. La comparación y las mediciones por canción están
en `output/guide-repetition-20261106/Informe.md`.
