# Fixture local de guía

El adjunto proporcionado por el usuario se copió explícitamente a
`output/section-detection-fixtures/Guia.mp3` (directorio ignorado por Git).
No se incorpora el audio al repositorio ni se transmite a servicios externos.

- SHA-256: `e9077dde87e9ae28ef70b2351411db11271ca026e3cf7acd406152db2db9966c`
- Tamaño: 11 440 810 bytes.
- Inspección con `afinfo`: MP3, estéreo, 44 100 Hz, duración estimada
  285,962450 segundos (aproximadamente 04:46).

Secuencia de referencia suministrada por el usuario:
conteo → coro → verso 1 → pre-coro → coro → verso 2 → pre-coro → coro →
puente → puente → instrumental → coro → puente → salida final → final.
Esperado: 4 coros, 2 versos, 2 pre-coros, 3 puentes, 1 instrumental,
1 salida final y 1 final. No hay intro anunciada.

`section_detection` comprueba únicamente normalización léxica de esa secuencia;
no transcribe el MP3 y no mide precisión ASR. El MVP de estructura excluye esta
pista de la mezcla musical. El incremento de transcripción local usa Whisper
sin necesitar el proveedor Apple: selecciona únicamente esta pista como Guía,
modelo multilingüe local e idioma `es`. Su resultado contiene texto e inicio/fin
de anuncios hablados; no crea ni alinea secciones musicales.

En el flujo de la app, Editar → **Sugerir secciones** usa el modelo local ya
preparado. Si falta, informa de la descarga única de unos 57 MiB y ofrece
**Descargar y continuar** antes del análisis. Ese GET nativo descarga solo el
modelo fijado y comprueba tamaño/SHA-256; no transmite la fixture. Tras el éxito
continúa el reconocimiento y entrega el resultado a la revisión sobre la onda.
La descarga requiere ese clic explícito, no ocurre al abrir la canción.

Como alternativa de desarrollo, preparar explícitamente el modelo desde la raíz:

```sh
python3 scripts/prepare_whisper_guide_model.py
python3 scripts/prepare_whisper_guide_model.py --verify-only
```

El script solo descarga `ggml-base-q5_1.bin` (aprox. 57 MiB), verifica SHA-256
fijado y no lee ni transmite esta fixture. No añade el modelo ni el audio a
Git. Véanse destino, hash y fuente upstream en
[`SectionDetection.md`](../../../docs/okf/audio-systems/SectionDetection.md#preparar-el-modelo-una-vez).
La inferencia posterior es local. La app mantiene el texto ASR crudo y sus
tiempos hablados; el borrador de cuadrícula/secciones de la onda es otra capa
revisable y no confirma automáticamente nada. Registrar por separado la primera inferencia
y una reutilización de caché; esta última no vuelve a probar el ASR.

Para evaluar alineación se necesitan los stems musicales y click sincronizados
del mismo proyecto, además de límites musicales etiquetados manualmente. No se
han recibido esos stems. No hay tiempos musicales de referencia ni métricas de
falsos positivos/negativos, y no se declara un porcentaje de precisión.

Registrar para cada anuncio en la futura prueba: texto finalizado, tiempo
hablado, límite musical real, compás, anticipación y error temporal; distinguir
errores ASR de errores de asociación. Nunca introducir los tiempos o etiquetas
esperados de este archivo dentro del algoritmo.

## Evidencia del incremento Whisper — 3 de noviembre de 2026

- Backend: `whisper.cpp 1.9.5:guide-energy-sinc16k-beam5-prompt128-2`.
- Modelo multilingüe `ggml-base-q5_1.bin`, SHA-256
  `422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898`.
- Probe directo, sin caché, idioma español y red bloqueada por `sandbox-exec`:
  `output/section-detection-fixtures/whisper-real-guide-v2.json`. Audio
  decodificado: 285,93632653 s; salida de 15 fragmentos con texto y tiempos.
  Se conservan dos errores léxicos observados (`Cordo!`, `Perso, dos.`).
- La inferencia se repitió por el servicio y panel de producción con una raíz
  temporal nueva y red bloqueada. `real-guide-panel.log` informa PASS; la captura
  `whisper-transcript-panel.png` muestra el resultado real, no el proveedor
  inyectado de las suites habituales.
- `whisper-cancel.json`: cancelación durante preparación, cero fragmentos.
  `whisper-silence.json`: WAV sintético estéreo a 48 kHz, 2 s de silencio y
  offset 2,5 s, cero fragmentos y duración final 4,5 s.

Las salidas anteriores permanecen en `output/` ignorado por Git. No hay medición
de alineación musical ni estimación de precisión general: los tiempos hablan
de la voz, y siguen faltando stems/click y límites musicales de referencia.
