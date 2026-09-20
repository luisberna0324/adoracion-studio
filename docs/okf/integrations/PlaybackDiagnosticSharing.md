# Compartir diagnóstico de reproducción

Implementación inicial: archivo local solicitado por el usuario. No hay
envío automático a soporte, chat, backend ni Firestore.

## Uso

Tras percibir un problema de reproducción: Ajustes → Diagnóstico →
**Compartir diagnóstico**.
La explicación visible indica que incluye ID de cuenta y datos técnicos,
sin audio ni credenciales. Android/iOS abre la hoja nativa para que el usuario
elija aplicación y destinatario. Escritorio permite guardar el ZIP y adjuntarlo
al chat. Cancelar la hoja no se presenta como envío exitoso.

La exportación mantiene la reproducción. Lectura, filtrado y compresión se
ejecutan fuera del hilo de audio y de interfaz. Hay un único envío/guardado
pendiente por editor; el botón permanece deshabilitado hasta terminar.

## Archivo v2

`PlayWorship-diagnostic-<UUID>.zip` contiene:

- `report.json`: schemaVersion, reportId, ID Firebase de cuenta (sin email),
  versión y fecha de compilación, SO/modelo, CPU/RAM, backend de audio,
  hora del reporte, cobertura de cada CSV y contadores de eventos descartados.
- `audio.csv`: callbacks, overruns, lecturas incompletas y generación de canción.
- `stems.csv`: métricas de lectura/decodificación/DSP, routing y contención por
  índice de pista, sin nombres ni archivos.
- `gaps.csv`: eventos de intervalos largos entre callbacks.
- `device.csv`: muestras de configuración y XRuns de la salida a 1 Hz.
- `distortion.csv`: peak/RMS, clipping, valores no finitos y discontinuidades
  de stems, master y salida final. No contiene muestras PCM.
- `markers.csv`: marcadores internos disponibles para compatibilidad. La
  interfaz ya no solicita al usuario marcar manualmente un crujido.
- `README.txt`: descripción del formato.

La única fuente son los seis archivos allowlisted por `AudioDiagnostics`.
No se empaquetan carpetas completas, logs de autenticación ni diagnósticos de
carga que puedan contener nombres o rutas. Sólo se aceptan filas CSV numéricas
completas, con el mismo número de columnas que su cabecera.

## Límites e interpretación

- Ventana solicitada: 120 s anteriores a **Compartir diagnóstico**.
- Máximo leído: 2 MiB por CSV; el manifiesto señala `byteLimited` y el intervalo
  efectivamente recuperado. Puede haber menos de 120 s en escenarios densos.
- Sólo sesión actual del motor; una reapertura del dispositivo inicia otra.
- El logger escribe cada 500 ms: la cola pendiente puede no aparecer aún.
  Una fila final sin salto de línea se excluye para evitar exportar una escritura
  incompleta. Falta de filas no se interpreta como ausencia de problemas.
- Los contadores del motor y de las pistas son acumulativos. No sumar filas.
  Segmentar por generación; en `stems.csv`, correlacionar los cambios con
  `audio.csv` y `device.csv` antes de comparar contadores.
- XRuns nativos no disponibles se representan con -1. El tamaño de bloque es
  el informado por JUCE; presupuesto/tamaño efectivo por callback se deduce de
  las métricas del motor y debe contrastarse con el dispositivo.
- El ID de cuenta permite buscar al usuario en el backend, pero un ZIP es
  un reporte aportado por el usuario, no prueba autenticada de identidad.
- Archivos temporales conservados para que el destinatario termine de leerlos;
  al generar otro reporte se limpian los generados hace más de 7 días.

La propuesta futura de carga autenticada y almacenamiento está en
[PlaybackDiagnosticsProposal](PlaybackDiagnosticsProposal.md). Esa API todavía
no existe. El backend `/Users/luisberna/dev/playworship` no cambia en esta fase.
