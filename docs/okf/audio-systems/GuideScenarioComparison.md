---
type: Test Contract
title: Comparación de reconocimiento antes y después
resource: scripts/compare_guide_scenarios.py
timestamp: 2026-11-04T00:00:00-03:00
---

# Comparar una corrección de reconocimiento

El conjunto descrito en [GuideDetectionTestSet.md](GuideDetectionTestSet.md)
conserva referencias fijadas antes de la inferencia. Después de modificar el
reconocimiento se ejecuta el mismo conjunto en otra carpeta y se comparan las
dos evaluaciones. El comparador no ejecuta ASR ni interpreta textos de guía.

```sh
python3.12 -B scripts/compare_guide_scenarios.py \
  --before output/guide-scenarios-20261104/run-001/evaluation.json \
  --after output/guide-scenarios-20261104/run-002/evaluation.json \
  --output-dir output/guide-scenarios-20261104/comparison-001-002

python3.12 -B scripts/compare_guide_scenarios.py --self-test
```

La carpeta de salida debe ser nueva o vacía. Ambos archivos deben tener el
mismo `suiteId`, IDs de casos/eventos, referencias completas (incluidas sus
tolerancias), SHA del audio de cada caso y huella de modelo. Se rechazan
evaluaciones parciales, resúmenes inconsistentes y mezclas de modelos o
proveedores dentro de una misma ejecución. Un cambio de versión del proveedor
entre ejecuciones es válido, se identifica explícitamente y permite evaluar
la adaptación de código manteniendo el mismo audio/modelo.

Se generan cuatro documentos:

- `comparison.md`: métricas, todos los casos y eventos, marca esperada,
  segundo de anuncio esperado, resultado anterior/nuevo y diferencia en ms.
- `comparison-events.csv`: todas las referencias y resultados por evento,
  tiempos hablados/propuestos, tolerancias, diferencias y razones.
- `comparison-cases.csv`: estados y aciertos por caso, más los eventos
  adicionales completos de cada ejecución.
- `comparison.json`: detalle y metadatos de ambas evaluaciones, incluidas
  políticas y SHA de los archivos de entrada.

Un evento se considera corregido si pasa de `passed=false` a `true`, y una
regresión si hace el recorrido contrario. Los fallos se distinguen como
ausentes, presentes fuera de tiempo o presentes incorrectos; esas categorías
son excluyentes. Los adicionales se muestran completos por caso. No se les
atribuye una identidad estable entre versiones ni se vuelve a emparejarlos
mediante similitud de texto.

El código de salida es `0` para una comparación válida sin regresiones, aunque
queden fallos anteriores; `2` si un evento/política empeora, un caso aprobado
deja de pasar o algún caso aumenta su número de adicionales; y `1` ante
entradas incompatibles o errores de herramienta. Por tanto, `0` no significa
que todo el reconocimiento sea correcto: hay que leer los fallos pendientes.

El conjunto inicial registró **19/24 casos y 74/89 marcas correctas**. Sus cinco
fallos consisten en 15 marcas ausentes (2 + 2 + 4 + 4 + 3) y cinco compuestos
adicionales revisables. La corrección debe recuperar cada marca con su tipo y
tiempo de referencia y eliminar el compuesto sustituido. Que cambie solamente
el texto del compuesto no acredita que se hayan recuperado sus eventos.

Estos informes reutilizan las decisiones del evaluador y no detectan por sí
solos una modificación interna de ese algoritmo que conserve el mismo esquema.
Para atribuir una mejora al reconocimiento, se conserva la versión del evaluador
y se revisa el diff de código además de los documentos. Tampoco acreditan los
gestos, reproducción o guardado de la app ni precisión en voces desconocidas.

La primera comparación real (`comparison-001-002/`) conserva el evaluador y
registra el cambio de proveedor 8 a 9: **19→21 casos aprobados, 74→79 marcas
correctas, 15→10 ausentes y 5→3 compuestos adicionales**. Cinco eventos se
corrigieron sin regresiones. `interludio-all-in-000`, `cuenta-es-116` y
`cuenta-en-120` siguen fallando y continúan visibles en las tablas completas.
