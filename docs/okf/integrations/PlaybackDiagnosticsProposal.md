# Diagnósticos de reproducción por usuario

Estado: propuesta técnica, 2026-09-17. No implementado ni desplegado.

La primera entrega se acotó a [compartir un ZIP por chat](PlaybackDiagnosticSharing.md).
Este documento conserva el diseño futuro de captura explícita y envío al backend.

## Objetivo

Permitir que un usuario reproduzca y reporte microcortes desde la app, sin
conectar el teléfono al equipo de desarrollo. Correlacionar el momento
reportado con lectura de stems, ejecución del motor y salida física de audio.

## Base existente

- `src/AudioDiagnostics.h`: eventos por callback, gaps, tiempos por etapa y
  por stem; escritura en segundo plano. Contrato en
  `../processor/AudioDiagnostics.md`.
- `src/ui/panels/SettingsPanel.*`: pantalla de ajustes y soporte.
- `src/data/AuthSessionApi.*`: sesión Firebase y llamadas autenticadas.
- Backend `/Users/luisberna/dev/playworship`: `lib/auth.ts`,
  `lib/firebase-admin.ts` y `lib/cloud-sync.ts`, con Firestore y R2.

## Experiencia propuesta

En Ajustes → Soporte: **Diagnosticar reproducción**.

1. Mostrar que se recogerán datos técnicos, estado de reproducción e identidad
   de la cuenta para soporte. No se capturan micrófono, canciones ni credenciales.
2. **Iniciar diagnóstico** abre una sesión acotada de hasta dos minutos.
3. Conservar la reproducción y ofrecer un indicador pequeño, con
   **Sentí un corte** y **Finalizar** accesibles desde la vista principal.
4. Cada marca guarda tiempo monotónico, posición de canción y contexto. Es una
   referencia humana aproximada, no el instante exacto del fallo.
5. **Enviar informe** permite agregar un comentario y devuelve un identificador
   copiable. **Descartar** elimina la sesión pendiente. Si no hay conexión,
   sólo un envío ya solicitado queda pendiente de reintento.

La primera versión es manual. La telemetría automática general de usuarios
queda fuera de su alcance.

## Captura

Extender el sistema canónico `AudioDiagnostics`; no crear otro logger por
callback. Buffers acotados y preasignados, productores separados cuando haya
hilos distintos. No JSON, red, compresión, disco ni esperas en el hilo de audio.
Un worker agrega métricas, comprime y prepara el envío al terminar.

Datos previstos:

- Cuenta: UID verificado por backend. Dispositivo: ID de instalación asociado
  a la cuenta, fabricante/modelo, SO, arquitectura, versión y build/commit.
- Salida: backend de audio, sample rate y tamaño de bloque efectivo, canales,
  latencia informada, tipo de salida cuando la plataforma lo permita.
- Motor: tiempo medio/p95/p99/máximo y presupuesto de callback; overruns,
  gaps entre callbacks, contadores de resets/reconstrucciones fallidas.
- Streaming: deltas de muestras faltantes, lecturas fallidas y contención del
  buffer por pista; tiempos de decodificación y recarga.
- Dispositivo: XRuns nativos y cambios de dispositivo/configuración. Consultar
  fuera del hilo de audio; indicar `unsupported` cuando no haya contador.
  No confundir el contador combinado de `AudioDeviceManager` con XRuns nativos.
- Contexto: posición, número/formato de stems, BPM original/actual, pitch,
  routing, mute/solo, CLICK/CLICK DYN; cambios de canción, seek, pausa y play.
  Identificar pistas/canciones con IDs técnicos; nombres y rutas se excluyen.
- Calidad de captura: eventos descartados, cobertura temporal y versión de
  esquema para no interpretar registros incompletos como ausencia de fallos.

Contadores acumulativos se convierten a deltas desde una línea base propia de
la sesión. Separar generaciones de dispositivo/canción: un contador que se
reinicia no es un delta negativo ni debe sumarse a todas sus muestras.

Para correlacionar eventos usar tiempo monotónico y una calibración a hora
UTC. Fechas de recepción las asigna el servidor. Estadísticas se calculan en
segundo plano; un percentil aproximado debe identificarse como tal.

## API y almacenamiento

Flujo propuesto:

`App → API autenticada → resumen en Firestore + trazas privadas en R2`

- `POST /api/diagnostics/prepare`: recibe ID UUID estable de informe, versión
  de esquema, resumen y tamaño/hash de la traza. Obtiene UID exclusivamente
  del token Firebase verificado. Asigna objeto privado y URL firmada acotada.
- `PUT` a URL firmada: carga la traza comprimida en segundo plano.
- `POST /api/diagnostics/commit`: verifica pertenencia, existencia, tamaño e
  integridad; valida eventos y finaliza el informe de forma idempotente.
- `GET /api/diagnostics/{reportId}`: estado visible sólo para su propietario
  o soporte autorizado. Acceso de soporte mediante permisos administrativos.

Propuesta Firestore:

```text
diagnosticReports/{reportId}
  uid, installationId, schemaVersion
  createdAt, receivedAt, expiresAt, status
  appVersion, buildId, platform, osVersion, deviceModel
  audioConfiguration
  durationMs, markerCount
  summary: { overruns, nativeXruns, streamingMissingFrames,
             streamingLockMisses, callbackP99Ms, droppedEvents }
  markers: [ eventos humanos acotados ]
  trace: { objectKey, sha256, sizeBytes }
```

Consultar por UID/fecha y por modelo/SO/build/fecha. Indexar sólo campos de
búsqueda; excluir mapas/arrays grandes. No almacenar una escritura por
callback en Firestore: el resumen es un documento por informe y la traza va a
almacenamiento de objetos. Límite documentado por Firestore: 1 MiB/documento.

Topes iniciales propuestos: 120 s de captura, 20 marcas y 5 MiB comprimidos por
informe. Aplicar también límite de tamaño descomprimido y esquema estricto.
Retención propuesta de 30 días, con TTL en Firestore y limpieza independiente
de objetos en R2, incluidos uploads abandonados. Configurar ambas políticas;
eliminar el documento no elimina automáticamente el archivo.

Reintentos con el mismo ID no crean informes adicionales. Límites por UID
deben ser compartidos entre instancias: el limitador actual en memoria del
backend no garantiza una cuota global en Cloud Run. Diagnósticos de soporte
no deben depender de la suscripción de sincronización de canciones.

## Interpretación

- Aumento de `streamingMissingFrames` junto a `streamingLockMisses`: evidencia
  de contención/lectura incompleta; revisar la pista y ventana temporal.
- Callback que supera presupuesto: evidencia de sobrecarga dentro del motor.
- XRuns nativos crecientes sin pérdidas del lector: investigar salida y
  planificación del dispositivo. No atribuir automáticamente al sistema operativo.
- Una marca sin anomalías registradas requiere revisar cobertura y métricas
  disponibles; no demuestra que el usuario no haya oído un corte.

## Validación de implementación

1. Forzar contención, falta de lectura y callback lento en pruebas y comprobar
   que cada informe conserva el motivo y el intervalo correcto.
2. Probar fin/cancelación/cambio de canción o dispositivo durante captura,
   ausencia de red, token vencido, reintento y cambio de cuenta.
3. Verificar aislamiento entre usuarios, cuotas compartidas, payloads inválidos,
   integridad del archivo y expiración independiente de metadatos/trazas.
4. Comparar carga de CPU, misses y callbacks con diagnóstico activo/inactivo
   en Android físico; no prometer coste cero. Después validar iOS y escritorio.
5. Capturar una sesión real del Moto Edge 50 con el síntoma reportado.

## Referencias

- https://firebase.google.com/docs/auth/admin/verify-id-tokens
- https://firebase.google.com/docs/firestore/quotas
- https://developer.android.com/games/sdk/oboe/low-latency-audio
