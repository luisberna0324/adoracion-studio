---
type: Test Contract
title: Reconocimiento de combinaciones rápidas de guía
resource: tests/fixtures/guide-scenarios/rapid-suite.json
timestamp: 2026-11-04T00:00:00-03:00
---

# Combinaciones rápidas y recuperación temporal

El proveedor 10 complementa Whisper local con una comparación acústica contra
las voces conocidas de `assets/guides/guides-en-es.zip`. Resuelve cuentas y
órdenes que Whisper junta o deja sin intervalos útiles. Conserva literalmente
el segmento ASR original y adjunta evidencia temporal separada. El contrato de
aceptación, cancelación y caché está en [SectionDetection.md](SectionDetection.md).
No se entrena Whisper ni se importan observaciones HTML como reglas de producción.

## Referencias congeladas

[rapid-suite.json](../../../tests/fixtures/guide-scenarios/rapid-suite.json)
contiene **30 casos y 116 eventos**: 62 cuentas, 25 principales y 29
indicaciones. El [guion temporal](../../../tests/fixtures/guide-scenarios/rapid-use-cases.md)
explica cada caso. Complementa los 24 escenarios de
[GuideDetectionTestSet.md](GuideDetectionTestSet.md), sin cambiar sus referencias.

Se combinan español, inglés e idiomas alternados; cuentas a 120/140/160/180
BPM; pausas activas de 200/100/60/40/20/0 ms; ordinales reales frente a Intro
seguido de un Dos; indicaciones consecutivas; volumen bajo, ruido y controles
sin voz. Trece casos tienen voces activas solapadas. Los 30 WAV suman
296,571375 s a 16 kHz mono PCM16. Se conserva la pronunciación original: el
estrés proviene de acercar los anuncios y reducir el intervalo entre pulsos.

Las referencias se fijan antes del ASR y sólo se entregan al evaluador. Tipo,
nombre, ordinal y posición deben coincidir dentro de **±40 ms**; esa tolerancia
no se amplió. Las principales van al inicio del compás siguiente al anuncio,
con la tolerancia de frontera contractual de 20 ms. Cuentas e indicaciones
conservan el instante hablado. El caso Intro + una sola cuenta no permite
completar números que no suenan ni convertir Intro en Intro 2.

- Especificación SHA-256:
  `da60c8775693713e830760ad7c64ec8b53a68bd1d38cc2ac54158e56639e100d`.
- Conjunto rápido:
  `50433b40e62df15458f7a1ca56f57581445cbbc0bab3ac129d89a3941a3ac949`.
- Pack SHA-256:
  `cc3c26dbc40e10a6c2fbc7ab518ffe1a4728a592f568a38b318f832f2014506b`.
- El evaluador conserva el snapshot anterior byte por byte:
  `cfd0d6e74c6527df480a933cf183089480a03e05b98045d4c0f04149746779cd`.

## Resultado de inferencia nueva

Las ejecuciones finales comenzaron el 5 de noviembre a las 01:40:50 UTC
(4 de noviembre por la noche local). Usaron el mismo modelo local
`ggml-base-q5_1.bin`, proveedor 10, cachés nuevas y red bloqueada. Los 54
resultados registran `cacheHit=false` y procedencia verificable del WAV y JSON.

| Conjunto | Antes: proveedor 9 | Después: proveedor 10 | Marcas correctas después | Extras después |
| --- | ---: | ---: | ---: | ---: |
| Original, 24 casos | 21/24 | 24/24 | 89/89 | 0 |
| Rápido, 30 casos | 8/30 | 30/30 | 116/116 | 0 |
| Total | 29/54 | **54/54** | **205/205** | **0** |

La comparación de referencias idénticas acredita **94 eventos recuperados**
(10 originales y 84 rápidos), cero regresiones de eventos/políticas y ningún
caso antes aprobado que empeore. El máximo error observado de las marcas es
**17,125 ms**, dentro de los 40 ms fijados. La correlación acústica y el residuo
de reconstrucción se muestran aparte: no son la confianza de Whisper ni un
porcentaje de exactitud universal.

La app y probes de macOS compilaron; la firma del bundle local se verificó.
**8/8 suites nativas** pasaron en 11,51 s: dominio, servicio/caché, panel,
selector, vocabulario, interpretación, plantillas acústicas y edición de onda.
Cubren también corrupción de metadatos, identidad del proveedor que cambia
durante el trabajo, cancelación, solapamientos, fases entre muestras y
ordinales que no deben convertirse en cuentas. Esto no reinicia la app ni
confirma un borrador del usuario.

## Guías reales

Se ejecutaron completas MI TESTIMONIO y VI LA LUZ con caché vacía y el mismo
proveedor/modelo. Se preservaron nombres, apariciones y fronteras principales.

| Guía | Principales | Cuentas | Indicaciones | Mayor error de cuenta en el inicio |
| --- | ---: | ---: | ---: | ---: |
| MI TESTIMONIO | 18 | 5 | 8 | 17,125 ms |
| VI LA LUZ | 19 | 5 | 7 | 7,293 ms |

Ambas conservan la secuencia realmente pronunciada **1, 2, 2, 3, 4**. Una
correlación independiente contra las grabaciones conocidas verifica los cinco
instantes iniciales de cada guía. No equivale a una anotación humana nueva de
toda la canción. No se modificaron los stems, arreglos ni la app abierta.

## Revisión entregada por el usuario

`guide-scenario-feedback-5c64ea1ae514f2de.json`, recibido en Descargas, contiene
96 decisiones confirmadas: 89 referencias y siete comprobaciones de casos,
distribuidas entre los 24 escenarios originales. No contiene notas ni cambios
de campos. Su SHA-256 es
`1b6626faf2b92d1e88a00ead4aea012dce6328ec22b21cdba1623055f8fe310f`.

Se conserva con su verificación en `human-review/`. Corresponde al informe del
proveedor 8 iniciado a las 00:07:56 UTC, no a una validación humana de los
resultados nuevos. No altera las puntuaciones automáticas ni el código.

## Reproducción y documentos

Usar Python 3.12 y un directorio de salida nuevo. El manifiesto del corpus
de voces aisladas debe haberse generado y verificado según el contrato del
conjunto original. No se requiere un servicio externo ni descargar otro modelo.

```sh
python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/rapid-suite.json \
  --output-dir output/guide-rapid-reproduction --dry-run
python3.12 -B scripts/build_guide_scenarios.py \
  --spec tests/fixtures/guide-scenarios/rapid-suite.json \
  --output-dir output/guide-rapid-reproduction --execute
python3.12 -B scripts/run_guide_scenarios.py \
  --manifest output/guide-rapid-reproduction/suite-manifest.json \
  --probe build/whisper-macos/MTControlsGuideTranscriptionProbe_artefacts/Release/MTControlsGuideTranscriptionProbe \
  --model "$HOME/Library/MTControls/models/ggml-base-q5_1.bin" \
  --output-dir output/guide-rapid-reproduction/run-010
python3.12 -B scripts/report_guide_scenarios.py \
  --manifest output/guide-rapid-reproduction/suite-manifest.json \
  --evaluation output/guide-rapid-reproduction/run-010/evaluation.json \
  --output-dir output/guide-rapid-reproduction/report-010
```

La [comparación](GuideScenarioComparison.md) exige mismo conjunto, audio,
referencias y modelo; permite cambiar la versión del proveedor. La evidencia
local final está en:

- `output/guide-scenarios-20261104/`: `run-003/`, `report-003/`,
  `comparison-002-003/` y `human-review/`.
- `output/guide-rapid-scenarios-20261104-final/`: `run-009/` y `run-010/`,
  `comparison-009-010/`, `report-010/`, `real-guides-010/` y
  `evidence-final-010/`.

El HTML permite escuchar los WAV y comparar referencia, ASR literal, evidencia
acústica y marcas finales. La exportación de observaciones sigue siendo insumo
para revisar el código; no aplica cambios a las canciones ni al modelo.

## Límites

El resultado acredita estas grabaciones conocidas y condiciones sintéticas,
además de las dos guías reales examinadas. No garantiza exactitud en voces
nuevas, otras mezclas o pronunciación más acelerada. Ante evidencia ambigua,
plantilla ausente o reconstrucción insuficiente se conserva la ruta ASR y la
revisión visual. La cobertura de voces ausentes del catálogo sigue declarada
en el conjunto original; no se atribuye una cobertura acústica a esos faltantes.
