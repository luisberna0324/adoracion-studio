# Validación de rendimiento Android — 2026-09-16

## Entorno

- Samsung Galaxy A50 (SM-A505G) físico, Android, salida estéreo.
- Rama: `codex/android-audio-performance`, cambios sobre `32bb594`.
- APK debug arm64 compilado e instalado por USB, conservando los datos.
- Callback observado: 1920 frames a 48 kHz, presupuesto de 40 ms.
- Sin restaurar el override de buffer eliminado de `StandaloneAudioDeviceBridge`.
- El checkout local de JUCE contiene cambios previos de Oboe externos a este
  repositorio; estas mediciones describen ese build local.

## Compatibilidad de callback Oboe — 2026-09-18

Una prueba A/B en un Motorola Edge 50 con Android 16 confirmó que la versión
original de `juce_Oboe_android.cpp` reproduce sin el crujido o los cortes
reportados. La variante local que llamaba
`AudioStreamBuilder::setFramesPerDataCallback(newBufferSize)` provocaba el
síntoma en ese dispositivo, incluso con un solo stem y usando el altavoz.

El build Android debe conservar el comportamiento original de JUCE/Oboe:
Oboe y su capa de quirks negocian el tamaño real del callback con el dispositivo.
No se debe forzar `framesPerDataCallback` para igualarlo al buffer solicitado
por `AudioDeviceManager`. El tamaño informado por el dispositivo puede seguir
registrándose en `audio_device_<ts>.csv`; la telemetría es observacional y no
debe modificar la configuración del stream.

La validación que aisló la causa mantuvo iguales la aplicación y su diagnóstico;
solamente cambió la implementación de Oboe entre ambas APK. Después de la
confirmación se restauró el archivo upstream y se hizo una compilación Android
limpia para impedir que una librería nativa anterior quedara reutilizada.

## Problemas corregidos

1. El agrupado sólo funcionaba si todas las pistas compartían destino. La
   configuración real usa click mono L, guía mono R e instrumentos estéreo.
   En salida estéreo se enruta cada pista antes de procesar hasta tres grupos
   de tono/tempo, conservando la separación de guía, percusión e instrumentos.
2. El renderer agrupado confundía la capacidad de scratch (8192) con el tamaño
   del callback (1920). Ahora recibe explícitamente el número de frames que
   debe producir. La capacidad reservada no cambia la duración del audio.
3. Al cambiar desde una canción con BPM modificado, el transporte volvía a
   ratio 1 pero el motor podía conservar el ratio anterior. Los lectores y el
   transporte derivaban y se reposicionaban repetidamente. Ahora los parámetros
   del motor se sincronizan antes de calcular el consumo de fuente.
4. Los VU Android pasaron de 8 a 30 Hz, con envolvente basada en tiempo real,
   actualización sólo del área de LEDs y omisión de medidores fuera del viewport.

## Medición comparable

“Alaba”: 19 stems, BPM 125 sobre original 127, pitch 0, routing mixto.
Ventanas de 1500 callbacks en reproducción (60 segundos), mismo dispositivo.

| Métrica | Antes | Después del agrupado corregido |
|---|---:|---:|
| Tiempo medio por callback | 35,511 ms | 10,049 ms |
| Percentil 99 | 41,158 ms | 23,221 ms |
| Máximo | 71,642 ms | 26,228 ms |
| Callbacks que exceden 40 ms | 21 | 0 |
| Incremento de misses de streaming | 0 | 0 |

Reducción del tiempo medio de aproximadamente 72 %. Los CSV de dispositivo
corresponden a las sesiones `20260916_151925` y `20260916_160438`, en
`Logs/MTControls/` del sandbox de la aplicación.

## Cambio de canción y pruebas de regresión

Prueba final sostenida, sesión `20260916_162114`: “Vi la luz”, 23 stems,
pitch −2 y BPM 124 sobre original 116 (ratio 1,068966). Incluye desplazamiento
horizontal del mixer. En 3030 callbacks / 121,16 segundos:

| Métrica | Resultado |
|---|---:|
| Tiempo medio | 12,502 ms |
| Percentil 99 | 17,211 ms |
| Máximo | 19,560 ms |
| Overruns | 0 |
| Misses de streaming iniciales/finales | 0 / 0 |
| Gaps de callback durante reproducción | 0 |

Después de la prueba se restauraron el tono A y los 116 BPM originales.

La secuencia Alaba a 125/127 → Vi la luz (23 stems) a BPM original reprodujo
las pérdidas de lectura. Tras sincronizar el ratio, la sesión
`20260916_161949` tuvo ocho misses concentrados en dos callbacks iniciales
del cambio/reinicio y ninguno nuevo en los siguientes 56 segundos.
No se interpreta ese arranque como una reproducción completamente libre de misses.

Pruebas automatizadas aprobadas:

- `stem_mix_engine`: routing mono a L/R con tono y tempo en los tres grupos;
  comparación de 23 stems entre buffers exactos y scratch de 8192, callbacks
  variables de 96/512/1920/960 frames, sin alterar el tail de scratch;
  alineación, fades y resets cubiertos por los casos existentes.
- `stem_buffering_source`.
- `bungee_processor`.
- `stem_output_router`.

## Límites de la validación

El agrupado conserva el contrato de canales y grupos, pero no es una suma
bit a bit de estiramientos individuales: Bungee no es lineal. Los dispositivos
multicanal con destinos heterogéneos y las guías con pitch explícito conservan
el procesamiento individual. Falta la valoración auditiva del usuario en su
salida habitual. Los tiempos de callback no sustituyen esa valoración.

La tasa configurada del VU es 30 Hz; no representa una garantía de 30/60 FPS
de toda la interfaz. SurfaceFlinger rondó 35 FPS en ambas versiones en las
primeras capturas, por lo que no se atribuye un aumento global de FPS al cambio.

## Seguimiento visual e inicio de canción

Segunda revisión del 2026-09-16:

- La caché Android usa hasta 2x de overscan y columnas ancladas al origen de la
  canción. Su renovación conserva el mismo muestreo y la posición de las crestas.
  Se eliminó la creación de una máscara de realce que Android no utilizaba.
- Restaurar el arrange ya no busca el timestamp de su primera sección. La carga
  vuelve a `Inicio` (0:00 por defecto) y reinicia la cámara y el suavizado visual.
  Los recortes de ventana guardados siguen siendo explícitos y se respetan.
- `waveform_cache` verifica cobertura, escala y posición de transitorios entre
  reconstrucciones, con varios anchos y niveles de zoom, incluidos extremos.
- `audio_engine_core` incluye una única sección en 3:52–4:15 y comprueba que
  el timeline anterior reproduce desde la fuente 0 sin salto automático.
- APK instalado en el Galaxy A50; captura de carga de “Vi la luz” muestra 00:00
  con sus secciones tardías conservadas.
- Comprobado también al cambiar a “Alaba” y volver: “Vi la luz” queda en 00:00,
  con follow conservado y la cámara al principio. El usuario confirmó que el
  seguimiento se ve fluido en esta versión.
- Sesión `20260916_163536`, seguimiento activo durante 110 segundos: cero
  overruns, dos misses en los primeros 80 ms y ninguno posterior. SurfaceFlinger
  informó 4966 frames, cero droppedFrames y 46,529 FPS medios; su histograma de
  presentación llegó al bucket de 70 ms. Es una medición de esta ejecución,
  no una comparación controlada de FPS ni una garantía de ausencia de tirones.
