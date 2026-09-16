# Fluidez Android — celular — 2026-09-15

## Resultado

Comparacion en `PlayWorship_Phone_API_36`, Android 16 / API 36, arm64,
2400 x 1080 landscape, con Glorioso dia (15 stems). Mismo AVD, biblioteca,
cancion y secuencia de gestos; instalaciones con `adb install -r`.

| Escenario | Antes: intervalo mediano entre cuadros | Despues | Cadencia equivalente antes / despues |
|---|---:|---:|---:|
| Reproduccion | 139.73 ms | 41.09 ms | 7.2 / 24.3 cuadros/s |
| Scroll horizontal del mixer | 124.54 ms | 50.16 ms | 8.0 / 19.9 cuadros/s |

La cadencia equivalente es `1000 / mediana`; no es un contador de FPS promedio.
Estos resultados corresponden al emulador, no garantizan el rendimiento de
un telefono fisico ni 60 FPS. El emulador se inicio con `-no-audio`: se verifico
el avance del transporte y los medidores, no la calidad audible de salida.

## Cambios

1. `assembleDebug` compila el codigo nativo con `-O2`, conservando los simbolos
   y el APK depurable. Antes no habia optimizacion en los comandos de Ninja.
   Una medicion intermedia, todavia con renderer software, redujo la mediana
   del dibujo (`SyncQueued - DrawStart`) de 129.21 a 31.39 ms en playback.
2. La raiz Android conecta un `OpenGLContext` de JUCE con render 2D por demanda.
   El framebuffer conserva las regiones no invalidadas. Se desconecta el
   contexto antes de destruir el editor y su arbol de componentes.
3. Cada slot del mixer conserva una imagen `SoftwareImageType`. Al desplazar
   canales se reutilizan las imagenes; VU, botones y volumen invalidan sus
   regiones. Los cambios de tamano fisico recrean el cache. Se reserva un
   presupuesto de 32 MiB para el cache de texturas del contexto.
4. Playback y pausa/stop cambian la representacion de la waveform. La
   transicion ahora invalida toda la onda antes de retomar el repaint parcial.

La prueba de `setBufferedToImage(true)` con imagenes GL anidadas empeoro el
rendimiento en el emulador y fue descartada. No forma parte del resultado.

## Metodo y limites de la medicion

- Baseline: fuentes de `0d2da61`, renderer software original, Debug sin `-O2`.
  Se recompilo ese codigo para la comparacion, sin usar el APK viejo que
  contenia otro overlay de diagnostico.
- Final: Debug `-O2`, OpenGL y cache software por slot, con los cambios de esta
  sesion. La comparacion final se hizo despues de terminar la compilacion de
  escritorio, sin builds ejecutandose en paralelo.
- Antes de cada escenario se limpiaron `dumpsys gfxinfo ... reset` y
  `dumpsys SurfaceFlinger --latency-clear`.
- Los botones Play y Stop se localizaron por sus bounds de accesibilidad.
  Se inicio desde Stop, se midieron 12 segundos de playback y luego cuatro
  gestos horizontales de 700 ms sobre la banda inferior del mixer.
- Los intervalos se calcularon restando timestamps consecutivos de presentacion
  (segunda columna de `SurfaceFlinger --latency`). Se excluyeron ceros y valores
  pendientes. Se eligio la superficie activa de la app con mas muestras.
- SurfaceFlinger conserva hasta 127 muestras: las capturas rapidas reflejan
  el tramo final del escenario. Las pausas deliberadas entre gestos impiden
  interpretar el percentil 95 del scroll como latencia de render.
- `gfxinfo` deja de medir el renderer nativo de JUCE al pasar a SurfaceView/GL.
  Sus cero cuadros no significan que la interfaz no se actualice. Por eso la
  tabla compara SurfaceFlinger en ambas variantes.
- Se comprobaron visualmente playback, pausa/stop, mute, cambio de cancion,
  scroll, popover, configuracion, setlist y retorno desde segundo plano.
- Build Android Debug correcto; build `MTControls_Standalone` macOS correcto.
  No se modificaron reglas de transporte, mezcla o ruteo.

## Reproducir el build

Desde `android/`:

```sh
./gradlew :app:assembleDebug
```

Para depuracion paso a paso sin optimizacion nativa:

```sh
./gradlew :app:assembleDebug -PplayworshipOptimizeDebug=OFF
```

La segunda opcion solo desactiva `-O2`; no restaura el renderer anterior.
El APK entregado esta en `android/app/build/outputs/apk/debug/app-debug.apk`.
Las capturas numericas de la comparacion estan junto a este informe.
