# Casos de uso para mejorar el reconocimiento de la guía

Este guion describe referencias que ya están congeladas en `suite.json` y en
el manifiesto del audio generado. No se ajusta a la transcripción de Whisper.
Los segundos siguientes se muestran redondeados a seis decimales; el manifiesto
conserva la precisión de frames a 16 kHz. La tolerancia de comparación es
±40 ms para estas entradas controladas. Una principal debe quedar en el
compás siguiente al del anuncio; una cuenta o indicación mantiene su tiempo.

## UC-01 — Cuenta en español e Intro independientes

Archivo: `cuenta-es-116.wav`. BPM 116, métrica 4/4, origen 0,500000 s.

| Evento | Texto hablado | Tipo / nombre esperado | Anuncio (s) | Marca esperada (s) |
| --- | --- | --- | ---: | ---: |
| one | Uno | Cuenta 1 | 0,500000 | 0,500000 |
| two-initial | Dos | Cuenta 2 | 1,534500 | 1,534500 |
| intro | Intro | Sección Intro, sin ordinal | 2,568938 | 4,637931 |
| two | Dos | Cuenta 2 | 3,086188 | 3,086188 |
| three | Tres | Cuenta 3 | 3,603438 | 3,603438 |
| four | Cuatro | Cuenta 4 | 4,120688 | 4,120688 |

Debe recuperar las cinco cuentas realmente pronunciadas. Los dos «Dos» son
eventos distintos. No debe crear Intro 2 ni completar otro Uno inexistente.

## UC-02 — La misma estructura en inglés

Archivo: `cuenta-en-120.wav`. BPM 120, métrica 4/4, origen 0,500000 s.

| Evento | Texto hablado | Tipo / nombre esperado | Anuncio (s) | Marca esperada (s) |
| --- | --- | --- | ---: | ---: |
| one | One | Cuenta 1 | 0,500000 | 0,500000 |
| two-initial | Two | Cuenta 2 | 1,500000 | 1,500000 |
| intro | Intro | Sección Intro, sin ordinal | 2,500000 | 4,500000 |
| two | Two | Cuenta 2 | 3,000000 | 3,000000 |
| three | Three | Cuenta 3 | 3,500000 | 3,500000 |
| four | Four | Cuenta 4 | 4,000000 | 4,000000 |

Las colas vocales pueden tocarse o solaparse. Un texto completo correcto sin
evidencia temporal para cada cuenta no acredita seis marcas correctas.

## UC-03 — Una principal y una secundaria consecutivas

BPM 116, métrica 4/4, origen 0,500000 s. En los tres archivos se pronuncia
Interludio a 0,620000 s; su entrada esperada es 2,568966 s, sin ordinal.

| Archivo | Pausa entre voces activas | Anuncio / marca de Toda la banda (s) |
| --- | ---: | ---: |
| interludio-all-in-200.wav | 200 ms | 1,660000 |
| interludio-all-in-060.wav | 60 ms | 1,520000 |
| interludio-all-in-000.wav | 0 ms | 1,460000 |

Cada archivo espera dos marcas. Toda la banda es una indicación `All In`,
independiente del límite de Interludio. Si el proveedor conserva una frase
compuesta revisable, las marcas ausentes permanecen como fallos de la prueba.

## UC-04 — Tres indicaciones sin secciones nuevas

Archivo: `indicaciones-consecutivas.wav`. BPM 120, 4/4, origen 0,500000 s.

| Texto hablado | Nombre esperado | Anuncio y marca (s) |
| --- | --- | ---: |
| Subir intensidad | Build | 0,620000 |
| All in | All In | 1,980000 |
| Entra batería | Drums In | 3,000000 |

Debe producir tres indicaciones, cero límites musicales y cero fragmentos
aislados como «All» o «In». La pausa interna de All In no crea otra marca.

## Comprobación y corrección

Para cada evento se conserva la referencia, el texto ASR literal, el significado
interpretado, el instante obtenido y su diferencia en milisegundos. Un resultado
sólo puede satisfacer una referencia. Las omisiones y los eventos adicionales
siguen visibles, aun cuando el resto del caso mejore.

Si aparece «For a frame» y PlayWorship lo resuelve como Refrain, la interpretación
puede ser correcta y el texto literal seguir mostrando un error de voz. Ese
tipo de reparación se cubre además en las regresiones del intérprete.

El ciclo es ejecutar, diagnosticar, modificar el componente responsable y
repetir el mismo WAV con el mismo modelo y referencias. Un fallo de la voz no
se soluciona cambiando su referencia ni entregando el guion al decoder. Las
decisiones de revisión sirven para adaptar el código; no se convierten en
reglas automáticas desde el informe.

El procedimiento completo y la matriz de 24 escenarios están en
[GuideDetectionTestSet.md](../../../docs/okf/audio-systems/GuideDetectionTestSet.md).
