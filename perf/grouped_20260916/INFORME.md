# Routing-agrupado — benchmark en Samsung Galaxy A50

Cambio aplicado en commit local sobre `src/PluginProcessor.cpp`:

- `routingGroupedEligible` se vuelve `true` cuando todos los stems
  activos resuelven al mismo `(channel, width)` y pitch/tempo están
  activos. En ese caso el callback deja de correr `BungeeProcessor::process()`
  una vez por stem y consume las tres voces
  `pitchShiftMelodic` / `tempoShiftPercussion` / `tempoShiftGuide` que
  `StemMixEngine` ya mantiene agrupadas. El resultado se enruta con
  una sola llamada a `StemOutputRouter`, plegando L+R cuando el destino
  común es mono.
- Si los stems divergen en `(channel, width)`, o si pitch/tempo están
  apagados, el procesador cae a la ruta por stem original.
- `AudioDiagnostics::StemTimingEvent` ahora incluye la columna
  `routing_grouped_eligible` para que el script distinga bloques
  agrupados vs no agrupados dentro del mismo escenario.

APK construido y desplegado:

```
android/app/build/outputs/apk/debug/app-debug.apk  (44.9 MB)
instalado en R58M81TZ45H (Samsung Galaxy A50)
```

## Baseline ya capturado en dispositivo

Ya existe en el dispositivo una sesión equivalente del 2026-09-15 (`audio_20260915_162701.csv`) que reproduce exactamente el escenario de la issue:

| Métrica | Valor |
|---|---|
| Tempo | 0.984252 (125/127) |
| Stems activos | 19 |
| Pitch semitonos | 0 |
| Bloques playing | 138.757 |
| `block_ms` avg | 3.36 ms |
| `block_ms` p99 | 10.86 ms |
| `block_ms` max | 16.24 ms |
| Overruns | 32,7 % |
| Bungee per block | 19 (todos los stems) |
| Bungee per stem avg | 227 µs |
| Gap events | 3 |

Estos números están dominados por el callback quantum de 2 ms heredado: la mediana de `block_ms` cabe, pero el **32,7 % de overruns** y el **19 stems × ~227 µs ≈ 4,3 ms de Bungee puro por bloque** confirman que el path legacy se rompe en cuanto el quantum sube a 40 ms (Galaxy A50 con el bridge configurado).

Esos CSVs ya están extraídos en `baseline_alaba_125/`.

## Captura nueva (pendiente — requiere acción del usuario)

Pasos para reproducir el escenario con la ruta agrupada activa:

1. Lanzar la app con la canción **ALABA**.
2. Ajustar el BPM de reproducción a **125** (ratio 0.984252).
3. Iniciar reproducción.
4. Capturar al menos 60 segundos de audio. La app escribe los CSVs en
   `/data/user/0/com.multitracks.playworship/Logs/MTControls/`. Para
   extraerlos:
   ```bash
   DEST=perf/grouped_20260916/grouped_alaba_125
   mkdir -p "$DEST"
   ADB=/Users/luisberna/Library/Android/sdk/platform-tools/adb
   for f in audio_*.csv audio_stems_*.csv audio_gaps_*.csv; do
     "$ADB" -s R58M81TZ45H shell run-as com.multitracks.playworship \
       cat /data/user/0/com.multitracks.playworship/Logs/MTControls/$f \
       > "$DEST/$f"
   done
   ```
5. Ejecutar el comparador:
   ```bash
   python3 perf/grouped_20260916/analyze.py
   ```
6. El comparador escribe `grouped_alaba_125/summary.txt` con el
   delta por métrica.

## Criterios de aceptación a verificar

- `block_ms` promedio claramente por debajo de 40 ms (target: ~6-10 ms).
- 0 o cercano a 0 gap events.
- 0 overrun blocks en el subconjunto `routing_grouped_eligible=1`.
- Bungee per-block = `0` en el CSV `audio_stems_*.csv` filtrado por
  `routing_grouped_eligible=1` (los tres voces suman en
  `stage_decode_render_us` total, no aparecen por stem).
- Sonido equivalente: ALABA en mono salida 1 con tempo 125 se
  escucha igual que antes; ningún stem queda inaudible, fade de
  click/guía sigue excluido, ruteo estéreo sigue funcionando si el
  usuario cambia el ancho del stem.
