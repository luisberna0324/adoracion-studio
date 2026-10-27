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
pista de la mezcla musical. La transcripción offline temporizada mediante
SpeechAnalyzer/SpeechTranscriber pertenece a la Entrega 2.

Para evaluar alineación se necesitan los stems musicales y click sincronizados
del mismo proyecto, además de límites musicales etiquetados manualmente. No se
han recibido esos stems. No hay tiempos musicales de referencia ni métricas de
falsos positivos/negativos, y no se declara un porcentaje de precisión.

Registrar para cada anuncio en la futura prueba: texto finalizado, tiempo
hablado, límite musical real, compás, anticipación y error temporal; distinguir
errores ASR de errores de asociación. Nunca introducir los tiempos o etiquetas
esperados de este archivo dentro del algoritmo.
